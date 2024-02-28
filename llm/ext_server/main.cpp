#include <iostream>
#include <cstring>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "ext_server.h"
#include "json-builder.h"

int main() {
    ext_server_params_t server_param = {
        .model = "/home/skyline/Projects/ollama/llm/onnxruntime-genai/phi2"
    };
    
    json_value * result = json_object_new(0);

    json_value * str = json_string_new("Hello");
    json_object_push(result, "prompt", str);

    char * json_req = (char *)malloc(json_measure(result));
    json_serialize(json_req, result);
    
    std::cout << json_req << std::endl;

    try {
    ext_server_resp_t resp;
    resp.msg_len = 512;
    resp.msg = (char *)malloc(512);

    llama_server_init(&server_param, &resp);
    std::cout << "llama_server_init" << std::endl;
    llama_server_completion(json_req, &resp);
    std::cout << "llama_server_completion" << std::endl;

    ext_server_task_result_t server_result;
    llama_server_completion_next_result(0, &server_result);
    std::cout << "llama_server_completion_next_result" << std::endl;

    std::cout << server_result.json_resp << std::endl;
    } catch (std::runtime_error & e) {
        std::cout << e.what() << std::endl;
    }
}