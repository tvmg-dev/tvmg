# TVMG Project Documentation
Welcome to the documentation for the ThermaV Monitoring Gadget.

## Author's Note
This project is the result of a three-year journey that began with a subpar LG Therma V installation. Faced with high energy bills and poor home comfort, it was nearly impossible to diagnose the system's inefficiencies without deeper technical insight.

Born out of necessity during the post-pandemic chip shortages, this cost-conscious monitoring tool was developed to bridge that data gap. The insights gained were transformative, allowing for a targeted redesign of the system's pipework. Today, the system operates at high efficiency, providing a consistently comfortable home. By open-sourcing this project, I hope to empower other LG homeowners to take control of their systems and achieve the same results.

P. Walton, April 2026

## System Overview

The heart of this software project is to collect information from an LG R32 Monobloc heat pump in order to provide homeowners greater insight into the operation of their system. This data collection is achieved by reading the LG's Modbus interface using this software running on a suitable microcontroller.

This monitoring system can also obtain data from the following sources:
- Some Shelly electrical power consumption meters
- PZEM-016 electrical power consumption meters
- DS18B20 temperature probes
- OpenWeather API for local temperature data

The data is collected every 30s and can be sent to a private account on [emoncms.org](https://emoncms.org/) where the data can be visualised in charts, added to dashboards and used in the heatpump application on that site.  Once the data is in the cloud then the historical data can be easily navigated in the site's charts.

The device software can be configured to send a daily email to the homeowner, and that will show the LG events that have been detected, for example when the compressor starts or stops, the current temperatures, defrosts etc.  This is a valuable tool in that it can show short cycling behaviour without requiring an emoncms account.

## Requirements

There are of course some basic requirements in order to use this software.
- A suitable controller.  The [Waveshare Industrial Relay](https://www.waveshare.com/esp32-s3-relay-1ch.htm) is recommended, and pre-built binary images are available for that device.
- A 2 wire connection between the controller and the LG outdoor unit.  It is recommended that outdoor-rated CAT5 or CAT6 cable is used for this connectivity. A 120 ohm resistor may be required between the LG's modbus terminals (given the low bitrate of the Modbus communication, and relatively short cable length, less than 50m, then a termination resistor is usually unnecessary).
- The LG outdoor unit requires two DIP switches to be set to a specific position.
- The controller requires a power supply.  A USB charger and type-C connector can be used.
- The device requires a good 2.4 GHz WiFi connection, and it is recommended that the home router assigns the same IP address to it.
- A laptop/phone/tablet that can connect to the device's 2.4 GHz WiFi Access Point when first commissioning the system.
- An email account that can be configured to send and receive emails.  GMail for example can be used for such purposes.

It is highly recommended that an emoncms account is provided so data can be sent to the account data 'feeds'.  The current cost of a single feed is £1 (+ VAT) per annum, subject of course to [emoncms.org](https://emoncms.org/) pricing.  It may be possible to use a self-hosted emoncms account but this cannot be guaranteed.  The homeowner can of course decide which data items should be sent to the cloud.

For a more comprehensive monitoring solution then it is recommended that the homeowner purchases the [Shelly Plus Add On](https://www.shelly.com/products/shelly-plus-add-on) which requires a compatible Shelly power meter such as the [Shelly EM Gen3](https://shellystore.co.uk/product/shelly-em-gen3/).  (Note that other Shelly devices can host the add-on accessory).  The power meter can be used to obtain electrical power consumption information, such as that consumed by the heat pump.  The add-on accessory provides connectivity for DS18B20 temperature probes.  Note that these DS18B20 are readily available but are typically using poorly performing copies of the AnalogDevices (Maxim) silicon - [OpenEnergy Monitor shop](https://shop.openenergymonitor.com/) usually have genuine parts in stock.  

With the recommended setup then the homeowner would have access to the following:
- Current LG operating mode, Fixed Heating, AI (and offset) or DHW
- LG temperatures - heating target, flow, return, outdoor, DHW target, DHW
- Advanced data (such as silent mode, compressor speed, refrigerant temperatures etc)
- Power Consumption
- External temperatures - such as independent flow & return for the heat pump and heating side (for example the temperatures recorded in a 4-pipe buffer configuration).

The series 4 models also provide flow rate information.  With this data then a series 4 homeowner can:
- obtain flow rate
- estimate thermal power generated
- estimate COP

**A Note on COP & Accuracy**

**Thermal power generation, and therefore COP, depends on the quality of data being recorded.**  If anti-freeze mixture is in use then the specific heat capacity of the fluid also has to be estimated and this introduces more inaccuracy.  Therefore the COP can only be a guide and cannot be used in place of a fully certified heat meter solution.  But it is useful to compare changes in efficiency when the homeowner adjusts settings, such as moving from fixed heating mode to weather compensation.

**Safety Note:** The homeowner should of course take every precaution when installing any electrical equipment.

## Developers

A developer can use this software and modify accordingly, taking into account the licensing of this project and the associated open source packages.  More documentation will be provided as time permits.

## Next Steps

This may appear daunting, especially to non-technical individuals.  But it is enlightening  to monitor the LG in realtime, with detailed charts.  The operation of your system may then be a pleasant surprise or a bit of a shock.


