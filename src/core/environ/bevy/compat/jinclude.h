// bevy port: jinclude.h is a libjpeg build-internal header that system
// libjpeg-turbo packages do not install. LoadJPEG.cpp only needs the C
// headers it pulls in.
#pragma once
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
