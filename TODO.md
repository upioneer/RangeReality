# Polish TODO

Deferred items, revisited once the functional POC is proven at the truck.

* OBD poll rate: 500 ms was too aggressive, back at 1000 ms. Retest faster
  rates against real adapter latency and timeout behavior.
* Themes: build kitt, minimalist, steampunk portrait layouts against the
  theme registry. Landscape variants after portrait acceptance.
* Orientation: verify portrait rotation mapping on the ST7789 panel, then
  enable the single press BOOT action for rotation toggle.
* Gauge: tune gradient stops and the 120 kW draw / 60 kW regen scales
  against real Lightning peaks.
* Session peaks: decide whether max values persist across power cycles.
