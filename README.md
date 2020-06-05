# Async MQTT library for ESP8266, ESP32 and STM32 using ArduinoIDE

Very "alpha" no TLS support and not yet tested on STM32

update: 6/5/2020: Tested on ESP32, seems fine but would appreciate some more testing on it...

use @ yer own risk etc


6/5/2020 v0.0.2  tidied code to resolve poor use of std::namespace (thanks @mknjc)
        decoupled garbage collector from KA timing to greatly reduce heap usage
          in very "noisy" environments (thanks @Adam Sharp)
        Optimised GC code for performance / size. Masive improvement in heap

