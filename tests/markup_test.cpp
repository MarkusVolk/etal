#include "catalog/markup.h"
#include "check.h"

#include <print>
#include <string>

namespace {
  void expect(const std::string& input, const std::string& wanted) {
    const std::string got = etal::appstreamToMarkup(input);
    if (got != wanted) {
      std::println(stderr, "input:  {}\nwanted: {}\ngot:    {}", input, wanted, got);
      std::exit(1);
    }
  }
} // namespace

int main() {
  expect("<p>One   two\n  three.</p>", "One two three.");
  expect("<p>First.</p><p>Second.</p>", "First.\n\nSecond.");
  expect("<p>Intro:</p><ul><li>a</li><li> b </li></ul><p>End</p>", "Intro:\n\n• a\n• b\n\nEnd");
  expect("<ol><li>one</li><li>two</li></ol>", "1. one\n2. two");
  expect("<p>A <em>big</em> <code>x &lt; y</code> &amp; more</p>", "A <i>big</i> <tt>x &lt; y</tt> &amp; more");
  expect("<p>Ünïcödé &#x263A; &#65;</p>", "Ünïcödé ☺ A");
  expect("<p>stray & and <b>bold</b></p>", "stray &amp; and bold");
  expect("<p>unclosed <em>italic</p><p>next</p>", "unclosed <i>italic</i>\n\nnext");
  expect("<ul><li>outer<ul><li>inner</li></ul></li></ul>", "• outer\n  • inner");
  expect("plain text without tags", "plain text without tags");
  expect("", "");
  CHECK(etal::escapeMarkup("a<b>&\"'") == "a&lt;b&gt;&amp;&quot;&#39;");
  return 0;
}
