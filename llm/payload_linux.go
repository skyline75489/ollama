package llm

import (
	"embed"
)

//go:embed llama.cpp/build/linux/*/*/lib/*.so*
//go:embed onnxruntime-genai/build/linux/*/*/lib/*.so*
var libEmbed embed.FS
