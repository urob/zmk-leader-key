# ZMK-LEADER-KEY

This module adds a `leader-key` behavior to ZMK. It is a reimplementation of
Nick Conway's [PR #1380](https://github.com/zmkfirmware/zmk/pull/1380). The most
important differences are:

- Works as a module without the need to patch ZMK.
- Sequences are `keycode`-based instead of `position`-based.
- Multiple leader-key instances with distinct sets of sequences are supported.
- Key codes that terminate the behavior are bubbled to other behaviors.
- Sequences inherit locality from the leader key (useful for `&sys_reset` and
  `&bootloader`).
- Strictly nested sequences are considered bad form and aren't supported.
- The `timeout`, `immediate-trigger` and `layers` properties are removed as
  their primary intend is to support nested sequences and to work around the
  single-instance restriction of the original PR.

A one-for-one port of the original PR version as a ZMK module is available in
the [`legacy`](https://github.com/urob/zmk-leader-key/tree/legacy) branch.

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
      remote: urob
      revision: v0.2+fix-child-nodes
      import: app/west.yml
    - name: zmk-leader-key
      remote: urob
      revision: v0.2 # set to same as ZMK version above
  self:
    path: config
```

Note: This module uses a version scheme that is synchronized with upstream ZMK.
To ensure compatibility, I highly recommend setting the revision of this module
to the same as ZMK's.

**Important:** The `zephyr` remote used by upstream ZMK currently contains a bug
that under certain circumstances causes the build to fail. You will need to
patch yours if your build fails with an error message like:

```
ERROR: /behavior/leader-key POST_KERNEL 31 < /behaviors/foo POST_KERNEL 49
```

The simplest way to get the patch is to use my `zmk` remote, as configured in
above manifest. This will automatically build against a patched version of
Zephyr. If you are building using Github Actions, you may need to clear your
cache (in the left sidebar on the `Actions` tab) for the changes to take effect.

## Configuration

### Leader sequences

Leader sequences are defined as child nodes of a leader-key instance. Each
sequence takes two arguments `sequence` and `bindings`. Example:

```c
/ {
    behaviors {
        leader1: leader1 {
            compatible = "zmk,behavior-leader-key";
            #binding-cells = <0>;
            usb { sequence = <U S B>; bindings = <&out OUT_USB>; };
            ble { sequence = <B L E>; bindings = <&out OUT_BLE>; };
            bt0 { sequence = <B N0>; bindings = <&bt BT_SEL 0>; };
            bt1 { sequence = <B N1>; bindings = <&bt BT_SEL 1>; };
            bt2 { sequence = <B N2>; bindings = <&bt BT_SEL 2>; };
            btc { sequence = <C L E A R>; bindings = <&bt BT_CLR>; };
            boot { sequence = <B O O T>; bindings = <&bootloader>; };
            reset { sequence = <R E S E T>; bindings = <&sys_reset>; };
        };

        leader2: leader2 {
            compatible = "zmk,behavior-leader-key";
            #binding-cells = <0>;
            de_ae { sequence = <A E>; bindings = <&de_ae>; };
            de_oe { sequence = <O E>; bindings = <&de_oe>; };
            de_ue { sequence = <U E>; bindings = <&de_ue>; };
        };
    };
};
```

This sets up two leader key instances, `leader1` and `leader2`. The first one
has various system related sequences, the second one is for German umlauts (the
`&de_*` bindings must be defined elsewhere).

**Note:** By default, modifiers in sequences are treated as a lower bound. For
example, holding `LSHFT` will trigger sequence `LS(A) LS(B)` as well as sequence
`A B`, whereas not holding any modifier will only trigger `A B`. This is so that
case-sensitive behavior bindings work as expected.

**Locality:** Sequence bindings inherit their locality from the position of the
leader key. For example, on split keyboards, the `BOOT` and `RESET` sequences in
the example above will invoke the bootloader on the side on which `&leader1` is
bound. Tip: Add `&leader1` to both sides to be able to reset either side.

### Behavior properties

**`ignore-keys`** (optional): If set to a list of key codes, these keys are
ignored when evaluating sequences. For instance, if
`ignore-keys = <LSHFT RSHFT>`, "shift" is passed through without triggering or
terminating sequences.

### `Kconfig` options

- `CONFIG_ZMK_LEADER_MAX_KEYS_PER_SEQUENCE`: Maximum number of keys in a
  sequence. Default is 5.
- `CONFIG_ZMK_LEADER_MAX_SEQUENCES`: Maximum number of sequences per leader key
  instance. Default is 32.

## References

- The [legacy](https://github.com/urob/zmk-leader-key/tree/legacy) branch ports
  Nick Conway's PR [#1380](https://github.com/zmkfirmware/zmk/pull/1380) as a
  ZMK module.
- My personal [zmk-config](https://github.com/urob/zmk-config) contains advanced
  usage examples.
