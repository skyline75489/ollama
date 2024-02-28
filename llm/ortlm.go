package llm

import (
	"io"
)

type ortModel struct {
	*containerORT
}

// NumCtx implements model.
func (*ortModel) NumCtx() uint32 {
	return 0
}

// NumEmbed implements model.
func (*ortModel) NumEmbed() uint32 {
	return 0
}

// NumGQA implements model.
func (*ortModel) NumGQA() uint32 {
	return 0
}

// NumHead implements model.
func (*ortModel) NumHead() uint32 {
	return 0
}

// NumHeadKv implements model.
func (*ortModel) NumHeadKv() uint32 {
	return 0
}

// NumLayers implements model.
func (*ortModel) NumLayers() uint32 {
	return 0
}

func newOrtModel(container *containerORT) *ortModel {
	return &ortModel{
		containerORT: container,
	}
}

func (llm *ortModel) ModelFamily() string {
	return "ort_transformers_1.18.0"
}

func (llm *ortModel) ModelType() string {
	return "transformers"
}

func (llm *ortModel) FileType() string {
	return "onnx"
}

func (c *containerORT) Name() string {
	return "ort"
}

func (c *containerORT) Decode(rso *readSeekOffset) (model, error) {
	model := newOrtModel(c)
	rso.Seek(0, io.SeekEnd)
	return model, nil
}
