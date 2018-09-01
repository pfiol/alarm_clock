# Wemos D1 Mini as Smart Alarm Clock

![Alarm Clock](https://xubecblog.files.wordpress.com/2018/06/20180619_102246.jpg?w=450&h=406)

## ESP8266 alarm clock.

Main characteristics:

* 4 digit 7 segment display.
* OLED to display the current and forecasted weather.
* NTP time.
* Weather Underground integration for weather forecasting.
* LED Strip sunrise effect for a nice wake up.
* Web-based configuration (alarm can be set by using external push buttons if needed).
* Weekly alarm scheduler.
* MQTT integrated button to toggle external element ON/OFF (I use it to turn on and off the lights on my bedroom).


To read a complete guide and see pictures please visit my blog: https://xubecblog.wordpress.com/2018/06/20/esp8266-based-smart-alarm-clock/

**IMPORTANT NOTE:** Please make sure you are running the latest  version of the ESP8266 Arduino IDE to compile the code. Versions previous to the 2.4.2 have a bug related to the use of the "tone" and "analogWrite" functions at the same time that causes strange behavior and annoying sound on the clock's buzzer. for more information on this bug please refer to the following site: https://github.com/esp8266/Arduino/issues/4349 
