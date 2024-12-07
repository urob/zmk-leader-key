# ZMK-LEADER-KEY

This module adds a `leader-key` behavior to ZMK. It is a reimplementation of
Nick Conway's [PR #1380](https://github.com/zmkfirmware/zmk/pull/1380). The most
important differences are:

- Works as a module without the need to patch ZMK.
- Sequences are `keycode`-based instead of `position`-based.
- Multiple leader-key instances with distinct sets of sequences are supported.
- Key codes that terminate the behavior are bubbled to other behaviors.
- Strictly nested sequences are considered bad form and aren't supported.
- The `timeout`, `immediate-trigger` and `layers` properties are removed as
  their primary intend is to support nested sequences and to work around the
  single-instance restriction of the original PR.

A one-for-one port of the original PR version as a ZMK module is available in
the `legacy` branch.

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
      revision: main
      import: app/west.yml
    - name: zmk-leader-key
      remote: urob
      revision: v0.1 # or 'main' for the latest, or 'legacy' for the original PR version
  self:
    path: config
```

**Important:** The `zephyr` remote used by upstream ZMK currently contains a bug
that under certain circumstances causes the build to fail. You will need the
patch if your build fails with an error message like:

```
ERROR: /behavior/leader-key POST_KERNEL 31 < /behaviors/foo POST_KERNEL 49
```

The simplest way to getting the patch is to use my `zmk` remote, as configured
in above manifest. This will automatically build against a patched version of
Zephyr. If you are building using Github Actions, you may need to clear your
cache (in the left sidebar on the `Actions` tab) for the changes to take effect.

## Configuration

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
            bt0 { sequence = <B 0>; bindings = <&bt BT_SEL 0>; };
            bt1 { sequence = <B 1>; bindings = <&bt BT_SEL 1>; };
            bt2 { sequence = <B 2>; bindings = <&bt BT_SEL 2>; };
            btc { sequence = <C L E A R>; bindings = <&bt BT_CLR>; };
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
has various bluetooth and output related sequences, the second one is for German
umlauts (the `&de_*` bindings must be defined elsewhere).

My personal [zmk-config](https://github.com/urob/zmk-config) has more advanced
examples.
