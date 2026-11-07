# Vendored design tokens

`tokens.json` is a verbatim copy of the Blackfyre design system's `project/tokens.json`.

- Source: <https://claude.ai/artifact/JX4X2WDSVT48H94TboBR6V>, file `project/tokens.json`
- Version: `1794044556-c45e`
- SHA-256: `25da75f563fb81140601fa92a83dde5ec8449bfdc84a38e2608f50016e35fb73`

The portal's page generator (`tools/portal/gen_pages.py`) takes every `bf-*` value from this file (the Paper, `light`, values). To update it, copy the newer file over this one, record its version and hash here, and run `mise run portal-check`.
