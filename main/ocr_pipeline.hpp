#pragma once

namespace app {

struct DemoImage;
struct UiContext;

// Decode the embedded image, run OCR, and publish the result through the UI module.
void run_ocr_pipeline(UiContext &ui, const DemoImage &selected_image);

} // namespace app
