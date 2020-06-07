# Async MQTT library for ESP8266, ESP32 and STM32 using ArduinoIDE

Very "alpha" no TLS support and not yet tested on STM32

update: 5/6/2020: Tested on ESP32, seems fine but would appreciate some more testing on it...

Testing / evaluation purposes only: use @ yer own risk etc


7/6/2020 v0.0.4  fixed reconnect bug (Thanks to Hamzah Hajeir)

6/6/2020 v0.0.3  major restructure: GC removed, TCP flow control replaces it
                VERY ROUGH, tons of diags in it, some scabby code..this is JUST to confirm previous nasty bug gone
                Qos0 ONLY!!!!

5/6/2020 v0.0.2  tidied code to resolve poor use of std::namespace (thanks @mknjc)
        decoupled garbage collector from KA timing to greatly reduce heap usage
          in very "noisy" environments (thanks @Adam Sharp)
        Optimised GC code for performance / size. Massive improvement in heap

