# Pinball Diag

A diagnostic tool for Pinball 2000 machines with a GUI. The program uses QT for
a simple GUI. The code is written in C++ to have good control over the timing.
Use this tool with a Linux machine to debug the Pinball 2000 driver board on a
bench.

## Screen shot

[screenshot](./img/pinballDiag_main.png)

## Dependencies

You will need a linux machine, I built this on Ubuntu 18.04.

You will need the follow packages installed to build the source code:

```
sudo apt install qt5-default qt5-qmake
```

## Building the project

You can open the project in QT Creator and build or use the following on the
command line.

Make a build directory outside of the source tree:

```
mkdir build-PinballDiag-Desktop-Debug
```

Build

```
cd build-PinballDiag-Desktop-Debug
qmake ../PinballDiag/PinballDiag.pro -r -spec linux-g++-64 CONFIG+=debug
make
```

## Run

The executable should be in the build directory. You need to run this as root 
because of the ioperm calls. This works under X but under Wayland you need to
allow the application to connect to the display manager. Use `xhost` to modify
the permissions, the command below will allow anything to connect.

```
xhost +
sudo ./PinballDiag
```

See the github wiki pages for more information.

## Authors

* **Curt Meyers** (http://www.boilerbots.com)

## License

This project is licensed under the BSD license - see the [LICENSE.txt](LICENSE.md) file.

