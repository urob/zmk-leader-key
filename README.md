# ZMK-LEADER-KEY

This module adds a `leader-key` behavior to ZMK.

For now, it is a one-for-one adaptation of Nick Conway's
[PR #1380](https://github.com/zmkfirmware/zmk/pull/1380). Going forward, I plan
to make a couple of breaking changes such as migrating from a position-based
sequence spec to a keycode-based one. The original PR version will continue to
be available in the `legacy` branch.

## Usage

To load the module, add the following entries to `remotes` and `projects` in
`config/west.yml`.

```yaml
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
    - name: urob
      url-base: https://github.com/urob
  projects:
    - name: zmk
      remote: zmkfirmware
      revision: main
      import: app/west.yml
    - name: zmk-leader-key
      remote: urob
      revision: main # or 'legacy' for the original PR version
  self:
    path: config
```

Unlike the original PR, the behavior won't be sourced automatically. To use it,
add the following line near the top of your keymap config:

```c
#include <behaviors/leader_key.dtsi>
```

## Configuration

Please see the original PR for the configuration details. My personal
[zmk-config](https://github.com/urob/zmk-config) has some hands-on examples.
