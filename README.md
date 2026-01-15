# hipi-ups-driver
Raspberry Pi Linux kernel driver for the HiPi / PiShop UPS Hat

## Setup

```sh
sudo apt install build-essential device-tree-compiler
```

## Usage

```sh
make
sudo make dts
sudo make load
```

Add to `/boot/firmware/config.txt`:

```
dtoverlay=hipi-ups
```
