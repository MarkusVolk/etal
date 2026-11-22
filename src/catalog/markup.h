#pragma once

#include <string>
#include <string_view>

namespace etal {
  // AppStream descriptions are a small XML language: <p>, <ul>/<ol> with <li>, <em> and <code>.
  // The result is Pango markup for a Label: paragraphs and lists separated by blank lines, items
  // as "• " or "1. " lines, <em> as italics and <code> as monospace. Unknown elements keep their
  // text; text is escaped, so the result is always valid markup.
  std::string appstreamToMarkup(std::string_view description);

  // Escapes &, <, > and quotes for Pango markup.
  std::string escapeMarkup(std::string_view text);
} // namespace etal
