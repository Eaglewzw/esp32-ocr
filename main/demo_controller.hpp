#pragma once

namespace app {

struct UiContext;

// Start the OCR worker and image selector. The UI must outlive the demo.
// Selection, busy-state checks, and return navigation are handled internally.
bool start_demo(UiContext &ui, int ocr_core_id);

} // namespace app
