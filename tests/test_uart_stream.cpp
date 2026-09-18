// Same shipped RTL with BIT overridden to 17 by Verilator. Shortening only
// the divider makes the previously cumulative one-cycle TX deficit reproduce
// in milliseconds rather than over 100 seconds of virtual board time.
#include "Vuart_echo.h"
#include "board/Uart.h"
#include "check.h"
#include "engine/VerilatorEngine.h"

#include <vector>

int main() {
  constexpr uint64_t bit = 17;
  constexpr uint64_t frame = 10 * bit;
  constexpr uint64_t start = 110;
  constexpr uint64_t count = 2000;
  auto engine = vb::makeVerilatorEngine<Vuart_echo>({.topModule = "uart_echo"});
  const auto reset = engine->lookup("btnC");
  const auto rx = engine->lookup("RsRx");
  const auto tx = engine->lookup("RsTx");
  CHECK(reset != vb::kNoSignal && rx != vb::kNoSignal && tx != vb::kNoSignal);
  engine->poke(rx, 1);
  engine->poke(reset, 1);
  engine->step(10);
  engine->poke(reset, 0);

  std::vector<uint8_t> expected, received;
  for (uint64_t i = 0; i < count; ++i)
    expected.push_back(static_cast<uint8_t>(i * 73 + 0x96));
  std::vector<uint64_t> completions;
  vb::UartTxDecoder decoder(bit);
  while (engine->now() < start + (count + 2) * frame) {
    const auto now = engine->now();
    bool input = true;
    if (now >= start && now < start + count * frame) {
      const uint64_t byteIndex = (now - start) / frame;
      const uint64_t bitIndex = ((now - start) % frame) / bit;
      input = bitIndex == 0 ? false : bitIndex == 9 ? true
          : ((expected[byteIndex] >> (bitIndex - 1)) & 1);
    }
    engine->poke(rx, input);
    engine->step(1);
    const auto result = decoder.sample(engine->now(), engine->peek(tx) != 0);
    CHECK(!result.framingError);
    if (result.byte) {
      received.push_back(*result.byte);
      completions.push_back(engine->now());
    }
  }
  CHECK_EQ(received.size(), expected.size());
  CHECK(received == expected);
  // Sustained TX must match incoming frame cadence. A queue only delays a
  // throughput deficit; it cannot repair a one-cycle gap after every frame.
  for (size_t i = 1; i < completions.size(); ++i)
    CHECK_EQ(completions[i] - completions[i - 1], frame);
  std::puts("test_uart_stream: PASS");
}
