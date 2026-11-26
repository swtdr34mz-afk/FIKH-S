# Credits

FIKH-S by swtdr34mz. All of its own code (the nine generators, the processor, the screens) is written for this
project, and all sounds are synthesized in the firmware: no samples. The concept, the names of the controls and the
look follow the Forge TME VHIKK X, the reverb's modes the Walrus Audio SLÖER; no code, sound or artwork of Forge TME
or Walrus Audio is used.

- The filter is a state-variable filter after Vadim Zavalishin's published topology-preserving form.
- The frequency shifter's 90° allpass pair uses the coefficients Olli Niemitalo published for a Hilbert transformer.

## Built on

| Project | Author | Licence | Used for |
| --- | --- | --- | --- |
| [fm1-base](https://github.com/charlesvestal/fm1-base) | Charles Vestal | MPL-2.0 | the platform: screen, panel, audio, MIDI, storage, simulator, build tools, the Terminus pixel fonts it carries (SIL OFL 1.1) |
| [felucca-bsp](https://github.com/hugelton/felucca-bsp) | Leo Kuroshita (Hügelton Instruments) and contributors | MPL-2.0 | the hardware layer, update loader, packaging |
| [Liberation Serif](https://github.com/liberationfonts) | Red Hat, Inc.; digitized data Google Corporation | SIL OFL 1.1 | the engraved serif labels (ui_serif.h, rendered by tools/gen_serif.py) |
| JieLi's AC79 SDK | JieLi Technology | Apache-2.0 | three files in every `.fwsc` package (fetched at build time, never copied here) |
