#include "ext_server.h"

#include "simdjson/simdjson.h"
#include "generators.h"
#include "tokenizer/tokenizer.h"
#include "models/model.h"
#include <span>

#include "json-builder.h"


struct ort_genai_client_slot
{
    int id;
    int task_id = -1;

   std::string prompt;
   std::string generated_text;

   void reset() {
    generated_text.clear();
   }
};

struct ort_genai_server_context {
    Generators::Model *model = nullptr;
    std::vector<ort_genai_client_slot> slots;

    void load_model(Generators::Model *model_) {
        model = model_;
    }

    void initialize() {
        const int n_parallel = 4;
        for (int i = 0; i < n_parallel; i++)
        {
            ort_genai_client_slot slot;

            slot.reset();

            slots.push_back(slot);
        }
    }

    ort_genai_client_slot* get_slot(int id) {
        return &slots[0];
    }

    std::vector<int> tokenize(const std::string & text) {
        auto tokenizer = model->CreateTokenizer();
        auto tokens = tokenizer->Encode(text.data());
        return tokens;
    }

    std::string detokenize(const std::span<int> & tokens) {
        auto tokenizer = model->CreateTokenizer();
        return tokenizer->Decode(tokens);
    }

    std::string generate(const std::string & prompt) {
        std::cout << "generate for prompt: " << prompt << std::endl;
        std::cout << "prompt length: " << prompt.size() << std::endl;
        auto search_params = Generators::GeneratorParams(*model);
        auto input_ids = tokenize(prompt);

        std::cout << "token length: " << input_ids.size() << std::endl;
        
        search_params.input_ids = input_ids;
        search_params.max_length = 50;
        search_params.batch_size = 1;
        search_params.sequence_length = input_ids.size();

        auto result = Generators::Generate(*model, search_params);
      
        std::cout << "Generate finished" <<  std::endl;

        return detokenize(result[0]);
    }
};


std::unique_ptr<OrtEnv> g_ort_env;
ort_genai_server_context* ort_genai = nullptr;
bool shutting_down = false;

OrtEnv& GetOrtEnv() {
  if (!g_ort_env) {
    Ort::InitApi();
    g_ort_env = OrtEnv::Create();
  }
  return *g_ort_env;
}


void llama_server_init(ext_server_params *sparams, ext_server_resp_t *err) {
  assert(sparams != nullptr && sparams->model != nullptr);

    auto provider_options = Generators::GetDefaultProviderOptions(Generators::DeviceType::CPU);
    OrtEnv& env = ::GetOrtEnv();
    Generators::Model* model = Generators::CreateModel(env, sparams->model, &provider_options).release();

    ort_genai = new ort_genai_server_context;
    ort_genai->initialize();
    ort_genai->load_model(model);
}

void llama_server_start() {

}

void llama_server_stop() {
  assert(ort_genai != NULL);
  shutting_down = true;
}

void llama_server_release_json_resp(char **json_resp) {
  if (json_resp == NULL || *json_resp == NULL) {
    return;
  }
  delete[] *json_resp;
}


void llama_server_tokenize(const char *json_req, char **json_resp,
                           ext_server_resp_t *err) {
  assert(ort_genai != NULL && json_req != NULL && json_resp != NULL && err != NULL);
  *json_resp = NULL;
  err->id = 0;
  err->msg[0] = '\0';
  try {
    simdjson::dom::element root;
    simdjson::dom::parser parser;
    simdjson::error_code error = parser.parse(std::string(json_req)).get(root);
    if (error) {
        std::string error_msg = simdjson::error_message(error);
        err->id = -1;
        snprintf(err->msg, err->msg_len, "%s", error_msg.c_str());
        return;
    }
    std::vector<int> tokens;
    std::string content;
    bool ret = tfm::TryToGetJson(root, "content", content);
    if (ret != simdjson::SUCCESS) {
        return;
    }
    tokens = ort_genai->tokenize(content);
    json_value * arr = json_array_new(0);
    for (const int& i: tokens) {
      json_array_push(arr, json_integer_new(i));
    }
    json_value * result = json_object_new(0);
    json_object_push(result, "tokens", arr);

    *json_resp = (char *)malloc(json_measure(result));
    json_serialize(*json_resp, result);
  } catch (std::exception &e) {
    err->id = -1;
    snprintf(err->msg, err->msg_len, "exception %s", e.what());
  } catch (...) {
    err->id = -1;
    snprintf(err->msg, err->msg_len, "Unknown exception during tokenize");
  }
}

void llama_server_detokenize(const char *json_req, char **json_resp,
                             ext_server_resp_t *err) {
  assert(ort_genai != NULL && json_req != NULL && json_resp != NULL && err != NULL);
  *json_resp = NULL;
  err->id = 0;
  err->msg[0] = '\0';
  try {
    simdjson::dom::element root;
    simdjson::dom::parser parser;
    simdjson::error_code error = parser.parse(std::string(json_req)).get(root);
    if (error) {
        std::string error_msg = simdjson::error_message(error);
        err->id = -1;
        snprintf(err->msg, err->msg_len, "%s", error_msg.c_str());
        return;
    }
    std::vector<int> tokens;
    bool ret = tfm::TryToGetJson(root, "tokens", tokens);
    if (ret != simdjson::SUCCESS) {
        return;
    }

    std::string content = ort_genai->detokenize(tokens);
    json_value * result = json_object_new(0);
    json_value * str = json_string_new(content.c_str());
    json_object_push(result, "content", str);

    *json_resp = (char *)malloc(json_measure(result));
    json_serialize(*json_resp, result);
  } catch (std::exception &e) {
    err->id = -1;
    snprintf(err->msg, err->msg_len, "exception %s", e.what());
  } catch (...) {
    err->id = -1;
    snprintf(err->msg, err->msg_len, "Unknown exception during detokenize");
  }
}

void llama_server_completion(const char *json_req, ext_server_resp_t *resp) {
  assert(ort_genai != NULL && json_req != NULL && resp != NULL);
  resp->id = 0;
  resp->msg[0] = '\0';
  try {
    if (shutting_down) {
      throw std::runtime_error("server shutting down");
    }

    std::cout << "parsing json" << std::endl;
    simdjson::dom::element root;
    simdjson::dom::parser parser;
    simdjson::error_code error = parser.parse(std::string(json_req)).get(root);
    if (error) {
        std::string error_msg = simdjson::error_message(error);
        snprintf(resp->msg, resp->msg_len, "%s", error_msg.c_str());
        return;
    }

    std::cout << "parsing json ok" << std::endl;

    ort_genai_client_slot *slot = ort_genai->get_slot(0);
    slot->prompt = std::string(std::string_view(root["prompt"]));
    
  } catch (std::exception &e) {
    snprintf(resp->msg, resp->msg_len, "exception %s", e.what());
  } catch (...) {
    snprintf(resp->msg, resp->msg_len, "Unknown exception during completion");
  }
}


void llama_server_completion_next_result(const int task_id,
                                         ext_server_task_result_t *resp) {

    resp->id = 1;
    resp->stop = false;
    resp->error = false;

    ort_genai_client_slot *slot = ort_genai->get_slot(0);

    std::string content = ort_genai->generate(slot->prompt);
    json_value * result = json_object_new(0);

    json_value * str = json_string_new(content.c_str());
    json_object_push(result, "content", str);
    str = json_string_new(slot->prompt.c_str());
    json_object_push(result, "prompt", str);
    
    resp->json_resp = (char *)malloc(json_measure(result));
    json_serialize(resp->json_resp, result);
}

void llama_server_release_task_result(ext_server_task_result_t *result) {
  if (result == NULL || result->json_resp == NULL) {
    return;
  }
  delete[] result->json_resp;
}

void llama_server_completion_cancel(const int task_id, ext_server_resp_t *err) {
  assert(ort_genai != NULL && err != NULL);
}
