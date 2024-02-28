package llm

import (
	"embed"
)

//go:embed onnxruntime-genai/build/linux/*/*/lib/*.so*
var libEmbed embed.FS
