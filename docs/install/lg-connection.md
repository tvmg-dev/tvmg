---
# LG connectivity
permalink: /docs/install/lg-connection/
---
[Home]({{ '/' | relative_url }})

# LG Setup

To enable LG modbus then it needs a physical 2 way wired connection to be made between the outdoor unit and the TVMG controller.  The LG proprietary communications protocol used for 3rd party controllers needs to be disabled and the open standard modbus protocol enabled for a slave device.  Finally the modbus address that the LG will appear on the bus needs to be assigned.

___

## Wiring

**Safety Note:** This requires the LG outdoor unit to be switched off and opened.  It is recommended that this be performed by a suitably qualified engineer, perhaps at the next service interval for the unit.

For many installation an outdoor grade CAT5/CAT6 cable is suitable  as only 2 wires are required, with one of the twisted pairs being used for the modbus A and B connections (e.g. blue for A and blue+white for B).  An engineer may recommend alternatives such as Belden 3106A and any grounding requirements (note that Modbus utilizes differential signaling; therefore, a dedicated ground connection is typically not required).  In practice the author has found outdoor CAT5 to be satisfactory and is readily available from the usual outlets such as Amazon. A 120 Ω (0.25 W) termination resistor should be installed across these terminals to mitigate electrical interference.  Again the author has found that to be unecessary given short cable runs and the low baud rate being used in the communication with the LG, but it does help signal integrity so is something to consider during this installation phase.

The connection to be made is to the 3rd party controller terminals of the heatpump.  These terminals are clearly marked within the LG unit but are dependent on the model variant (see below).  **Ensure** the wiring of the A and B terminals is documented to maintain correct polarity with the Modbus master.

### Series Type

There are (at least) 3 different variants of the R32 ThermaV monobloc heatpump to consider.  The **series 3**, which has case 1 and case 2 types, and the later **series 4**.  The model number of the unit can be used to determine whether it is a series 3 or series 4 unit.
The installation manual available at [LG Support](https://www.lg.com/uk/support/product-support/manuals-software/){:target="_blank" rel="noopener noreferrer"} (select Heating & Cooling, then AWP and select your model) provides in depth information regarding the heat pump.  The following breakdown of the model number is shown in the manual (it is the same for all monobloc types).  This doesn't actually indicate series 3 or 4 in the breakdown, but it is the highlighted value at position 8.

<div style="display: flex; gap: 20px; align-items: flex-start; ">
  <img src="{{ '/docs/install/images/model-id.jpg' | relative_url }}" style="max-height: 400px; width: auto;">
</div>

For example, the HM121MU34 model number is a 12 kW series 4 unit.

From the LG installation manual the relevant terminal block connections are shown for each model in the following sections.

### Series 3 : Case 2 (up to August 2020)

<div style="display: flex; gap: 20px; align-items: flex-start; ">
  <img src="{{ '/docs/install/images/series-3-case2.jpg' | relative_url }}" style="max-height: 400px; width: auto;">
</div>

### Series 3 : Case 1 (From August 2020)

<div style="display: flex; gap: 20px; align-items: flex-start; ">
  <img src="{{ '/docs/install/images/series-3-case1.jpg' | relative_url }}" style="max-height: 400px; width: auto;">
</div>

### Series 4

<div style="display: flex; gap: 20px; align-items: flex-start; ">
  <img src="{{ '/docs/install/images/series-4.jpg' | relative_url }}" style="max-height: 400px; width: auto;">
</div>

___
## LG DIP Switch Settings

With the physical connection made to the LG then the next step is to ensure that DIP switches in the unit have been set such that the LG will be acting as a modbus slave device, and using the standard modbus protocol.  For both series 3 and 4 then switch SW1 needs to be located.  Switches do have labels printed on the circuit board, and also usually have the ON/OFF positions clearly marked.  The individual DIP switches must be set correctly for correct communications.

### Series 3

DIP switch 1 and 2 of the SW1 PCB switch must be set to the ON position, which is likely opposite to the default settings.  The following shows the settings and switches from the installation manual.

\*Note: It isn’t known whether the PCB layout is different between the case 1 and case 2 variants of the series 3 ThermaV.

<div style="display: flex; gap: 20px; align-items: flex-start; ">
  <img src="{{ '/docs/install/images/series-3-dip.jpg' | relative_url }}" style="max-height: 500px; width: auto;">
</div>


### Series 4

Similar to the series 3, for SW1 both DIP switch 1 and 2 must be set to the ON position.

<div style="display: flex; gap: 20px; align-items: flex-start; ">
  <img src="{{ '/docs/install/images/series-4-dip.jpg' | relative_url }}" style="max-height: 700px; width: auto;">
</div>

___
## LG Controller Setting

After the electrical connections have been made to the LG and the DIP switches set to their correct positions then the unit should be re-started and a check made that the unit is operating normally.  The LG controller will still be able to communicate with the outdoor unit and control operation as before.  

Before interfacing any modbus master device to the LG, then its slave address must be set.  This requires chnages via the installer menu of the LG controller.  To access the installer menu then, using the arrow keys select the Menu icon, enter this menu and select the Settings menu.  In the bottom right corner is the software version, e.g. 3.0.6.4.  Now press and hold the up arrow button for 5 seconds and a password screen will be displayed, the password is the software version - e.g. 3064. Use the navigation buttons to set to the passowrd values and then click OK to enter the installer menus. (See [YouTube](https://www.youtube.com/shorts/38_ELQAOGpc){:target="_blank" rel="noopener noreferrer"} for a brief demonstration of entering the installer menu).

The modbus address must be a unique value for devices on the bus.  If using a simple installation with no other modbus devices then the recommendation is to set the value to 08.  Otherwise ensure the address is not being used by any other device on your modbus network.

## Series 3

Probably using software revision 3.0.5.3 to 3.0.5.6.  Scroll down the installer menus and select the Modbus Address item.  

## Series 4

For series 4 models, software version likely to be in the 3.0.6.x range.  The modbus address is set via the Connectivity → Modbus Address setting.

___
## Installation Videos

The [Homely Energy](https://www.youtube.com/watch?v=_E2PxnD7P_g){:target="_blank" rel="noopener noreferrer"} video on YouTube shows installation guidance, showing model differences and wiring of a series 4.  There's also another YouTube video from [Rod McBain](https://www.youtube.com/watch?v=Xuj2YFZ5zME){:target="_blank" rel="noopener noreferrer"} that shows his connectivity to a series 3 unit.  Credit to [Homely](https://www.homelyenergy.com/){:target="_blank" rel="noopener noreferrer"} and Rod McBain.

___
# Next Steps

At this stage the LG has been wired for modbus operation and its slave address changed to a unique value.  The unit should now be closed ready for normal operation as all the changes necessary for the LG have been completed.  
