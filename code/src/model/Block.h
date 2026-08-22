#pragma once
#include "PsramAlloc.h"

class Renderer;
class Epub;

typedef enum
{
  TEXT_BLOCK,
  IMAGE_BLOCK
} BlockType;

// a block of content in the html - either a paragraph or an image
class Block
{
public:
  // PSRAM-backed (see PsramAlloc.h) — one of these exists per paragraph,
  // and derived classes (TextBlock, ImageBlock) don't declare their own
  // operator new, so this applies to them too.
  static void *operator new(size_t sz) { return model_alloc(sz); }
  static void operator delete(void *p) noexcept { free(p); }

  virtual ~Block() {}
  virtual void layout(Renderer *renderer, Epub *epub, int max_width = -1) = 0;
  virtual void dump() = 0;
  virtual BlockType getType() = 0;
  virtual bool isEmpty() = 0;
  virtual void finish(){};
};
