#include "catalog/markup.h"

#include <charconv>
#include <cstdint>
#include <vector>

namespace {
  void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x110000) {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }

  // The entity at `text` (which starts with '&') as UTF-8, and how many bytes it took; a stray
  // '&' stands for itself.
  std::pair<std::string, std::size_t> entity(std::string_view text) {
    const auto end = text.find(';');
    if (end == std::string_view::npos || end > 12) {
      return {"&", 1};
    }
    const std::string_view name = text.substr(1, end - 1);
    if (name == "amp") {
      return {"&", end + 1};
    }
    if (name == "lt") {
      return {"<", end + 1};
    }
    if (name == "gt") {
      return {">", end + 1};
    }
    if (name == "quot") {
      return {"\"", end + 1};
    }
    if (name == "apos") {
      return {"'", end + 1};
    }
    if (name.size() > 1 && name[0] == '#') {
      const bool hex = name[1] == 'x' || name[1] == 'X';
      const std::string_view digits = name.substr(hex ? 2 : 1);
      std::uint32_t cp = 0;
      const auto [ptr, error] = std::from_chars(digits.data(), digits.data() + digits.size(), cp, hex ? 16 : 10);
      if (error == std::errc() && ptr == digits.data() + digits.size()) {
        std::string out;
        appendUtf8(out, cp);
        return {out, end + 1};
      }
    }
    return {"&", 1};
  }

  bool isSpace(char c) { return c == ' ' || c == '\n' || c == '\t' || c == '\r'; }

  class Converter {
  public:
    std::string run(std::string_view input) {
      std::size_t i = 0;
      while (i < input.size()) {
        const char c = input[i];
        if (c == '<') {
          const auto close = input.find('>', i);
          if (close == std::string_view::npos) {
            break;
          }
          tag(input.substr(i + 1, close - i - 1));
          i = close + 1;
        } else if (c == '&') {
          const auto [text, length] = entity(input.substr(i));
          this->text(text);
          i += length;
        } else if (isSpace(c)) {
          m_pendingSpace = true;
          ++i;
        } else {
          this->text(std::string_view(&input[i], 1));
          ++i;
        }
      }
      finishLine();
      finishBlock();
      std::string out;
      for (const auto& block : m_blocks) {
        if (!out.empty()) {
          out += "\n\n";
        }
        out += block;
      }
      return out;
    }

  private:
    void text(std::string_view text) {
      if (m_pendingSpace && m_hasText) {
        m_line += ' ';
      }
      m_pendingSpace = false;
      for (const char c : text) {
        switch (c) {
        case '&':
          m_line += "&amp;";
          break;
        case '<':
          m_line += "&lt;";
          break;
        case '>':
          m_line += "&gt;";
          break;
        default:
          m_line += c;
        }
      }
      m_hasText = true;
    }

    void open(const char* pango) {
      if (m_pendingSpace && m_hasText) {
        m_line += ' ';
      }
      m_pendingSpace = false;
      m_line += '<';
      m_line += pango;
      m_line += '>';
      m_open.push_back(pango);
    }

    void close(const char* pango) {
      // Close what was opened after it too: the input may nest badly, the output may not.
      while (!m_open.empty()) {
        const std::string last = m_open.back();
        m_open.pop_back();
        m_line += "</" + last + ">";
        if (last == pango) {
          break;
        }
      }
    }

    void tag(std::string_view body) {
      const bool closing = !body.empty() && body.front() == '/';
      if (closing) {
        body.remove_prefix(1);
      }
      if (!body.empty() && body.back() == '/') {
        body.remove_suffix(1);
      }
      const auto nameEnd = body.find_first_of(" \t\n/");
      const std::string_view name = body.substr(0, nameEnd);
      if (name == "p") {
        finishLine();
        finishBlock();
      } else if (name == "ul" || name == "ol") {
        finishLine();
        if (!closing) {
          if (m_lists.empty()) {
            finishBlock();
          }
          m_lists.push_back(name == "ol" ? 1 : 0);
        } else {
          if (!m_lists.empty()) {
            m_lists.pop_back();
          }
          if (m_lists.empty()) {
            finishBlock();
          }
        }
      } else if (name == "li") {
        finishLine();
        if (!closing) {
          m_line.assign(m_lists.empty() ? 0 : 2 * (m_lists.size() - 1), ' ');
          if (!m_lists.empty() && m_lists.back() > 0) {
            m_line += std::to_string(m_lists.back()++) + ". ";
          } else {
            m_line += "• ";
          }
        }
      } else if (name == "em" || name == "i") {
        closing ? close("i") : open("i");
      } else if (name == "code") {
        closing ? close("tt") : open("tt");
      } else if (name == "br") {
        finishLine();
      }
    }

    void finishLine() {
      while (!m_open.empty()) {
        close(m_open.back().c_str());
      }
      if (m_hasText) {
        if (!m_block.empty()) {
          m_block += '\n';
        }
        m_block += m_line;
      }
      m_line.clear();
      m_hasText = false;
      m_pendingSpace = false;
    }

    void finishBlock() {
      if (!m_block.empty()) {
        m_blocks.push_back(std::move(m_block));
      }
      m_block.clear();
    }

    std::vector<std::string> m_blocks;
    std::string m_block;
    std::string m_line;
    std::vector<std::string> m_open;
    // Per open list: 0 for <ul>, else the number of the next <ol> item.
    std::vector<int> m_lists;
    bool m_pendingSpace = false;
    bool m_hasText = false;
  };
} // namespace

std::string etal::appstreamToMarkup(std::string_view description) { return Converter().run(description); }

std::string etal::escapeMarkup(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    switch (c) {
    case '&':
      out += "&amp;";
      break;
    case '<':
      out += "&lt;";
      break;
    case '>':
      out += "&gt;";
      break;
    case '"':
      out += "&quot;";
      break;
    case '\'':
      out += "&#39;";
      break;
    default:
      out += c;
    }
  }
  return out;
}
