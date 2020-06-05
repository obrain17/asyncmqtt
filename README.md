# Async MQTT library for ESP8266, ESP32 and STM32 using ArduinoIDE

Very "alpha" no TLS support and not yet tested on ESP32 or STM32

use @ yer own risk etc


v0.0.2  tidied code to resolve poor use of std::namespace (thanks @mknjc)
        decoupled garbage collector from KA timing to greatly reduce heap usage
          in very "noisy" environments (thanks @Adam Sharp)
        Optimised GC code for performance / size. Masive improvement in heap

