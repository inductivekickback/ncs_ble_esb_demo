This project was built from the v1.6.0 tag of [nRF Connect SDK (NCS)](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/index.html) and demonstrates a reasonable approach to integrating the [Enhanced ShockBurst (ESB) Transmitter sample](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/samples/esb/README.html) into the [Bluetooth Peripheral UART sample](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/samples/bluetooth/peripheral_uart/README.html) so the two protocols can run concurrently. An expanded description is posted [here](https://devzone.nordicsemi.com/nordic/nrf-connect-sdk-guides/b/software/posts/updating-to-the-mpsl-timeslot-interface).

The BLE connection looks like this:
<p align="center"><img src="https://user-images.githubusercontent.com/6494431/127812915-d1d09291-e4d5-47c2-bcce-922ccc5651a2.gif" width="1024"></p>

And the timeslots fit in to the schedule like this:
<p align="center"><img src="https://user-images.githubusercontent.com/6494431/127733643-084f0694-cbba-405c-8e93-ffa147135ae9.gif" width="1024"></p>

##### Features
* Wraps the [MPSL timeslot](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrfxlib/mpsl/doc/timeslot.html) feature to provide a simple interface
  * Provides fixed-length timeslots at a consistent interval
* Uses the [MPSL radio notifications](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrfxlib/mpsl/doc/radio_notification.html) feature to synchronize timeslots to BLE Connection Events
* Optimized SoC peripheral use
  * a single interrupt vector is used for both radio notifications and timeslot callbacks
  * a single (cooperative) thread
    * minimizes processing in ISRs
    * executes application callbacks in a safe manner
    * protects MPSL API calls from being reentered
  * no additional RTCs or timers
* Requires minimal modification to the ESB library
  * doesn't add RADIO_IRQHandler to the vector table (this is already done by the SoftDevice Controller)
  * two (optional) functions allow saving and restoring a pipe's PID so it can persist when the library is disabled and reinitialzed
* BLE connectivity can be tested using nRF Connect for Mobile ([Android](https://play.google.com/store/apps/details?id=no.nordicsemi.android.mcp&hl=en_US&gl=US), [iOS](https://apps.apple.com/us/app/nrf-connect-for-mobile/id1054362403))
* [ESB](https://devzone.nordicsemi.com/nordic/nordic-blog/b/blog/posts/intro-to-shockburstenhanced-shockburst) works with the (unmodified) Enhanced ShockBurst Receiver sample in NCS
