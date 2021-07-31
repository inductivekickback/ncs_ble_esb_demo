This project demonstrates a straightforward approach to integrating the [Enhanced ShockBurst (ESB) Transmitter sample](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/samples/esb/README.html) into the [Bluetooth Peripheral UART sample](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/samples/bluetooth/peripheral_uart/README.html) so the two protocols can run concurrently. It was built from the v1.6.0 tag of [nRF Connect SDK (NCS)](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/index.html).

##### Features
* Wraps the [MPSL timeslot](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrfxlib/mpsl/doc/timeslot.html) feature to provide a simple interface
* Uses the [MPSL radio notifications](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrfxlib/mpsl/doc/radio_notification.html) feature to synchronize timeslots to BLE Connection Events
* Optimized SoC peripheral use
  * a single hardware interrupt vector is used for both radio notifications and timeslot callbacks
  * a single thread executes application callbacks in a safe manner
  * no additional RTCs or timers
* Requires minimal modification to the ESB library
  * RADIO_IRQHandler does not need to be added to the vector table (this is done by the SoftDevice Controller)
  * two simple functions allow saving and restoring a PIPE's pid so it can persist when the library is disabled and reinitialzed
* BLE connectivity can be tested using nRF Connect for Mobile ([Android](https://play.google.com/store/apps/details?id=no.nordicsemi.android.mcp&hl=en_US&gl=US), [iOS](https://apps.apple.com/us/app/nrf-connect-for-mobile/id1054362403))
* ESB works with the (unmodified) Enhanced ShockBurst Receiver sample in NCS
