---
# Features Top Level
permalink: /docs/features/features/
---
[Home]({{ '/' | relative_url }})

## Features

### LG Event Log

The LG controller captures some events that occur during operation, starting and stopping heating or DHW cycles for example. But this logging information is crude and has a catpure depth of only 50 events.  The TVMG software collects more event infomation and can be configured to send a daily update email that captures up to 600 events per day.

<div style="display: flex; gap: 20px; align-items: flex-start; ">
  <img src="{{ '/docs/features/images/event-log.jpg' | relative_url }}" style="max-height: 700px; width: auto;">
</div>

During April this unit was configured for
- Weather compensation heating between 4am and 8am, with -2 set on the LG controller.
- DHW heating to 45°C starting from 5:45am until 7am, 30°C for the rest of the day.
- A hotter DHW cycle from 1pm-2pm on Monday, raising water to 58°C.
- silent mode operation set for 23 hours of the day, with normal operation between 3 and 4pm.

This data log shows the unit responding to how it has been configured.  At 4am the unit starts, with the pre-run of the water pump taking a couple of minutes before the compressor becomes active.  The weather compensation settings combined with the AI offset have resulted in a target temperature of 24°C.  DHW is at 40.5°C compared with the current target of 30°C.  Silent mode indicator is on.

At 5:45am the unit starts to switch over to the DHW schedule.  The compressor switches off, then is restarted for DHW a few minutes later.  At 6:15sm the DHW has reached 48.2° and the DHW cycle ends, and switches back to weather compensated heating.  We see new events as the target temperature climbs from 24°C to 25°C as the outdoor temperature changes, and the target returns back to 24°C due to outdoor conditions.

At 7:42am a defrost cycle starts and runs for a few minutes, the inlet water temperature is now hotter than the outlet as the refrigerant cycle has reversed for the defrost.  At 8am the heating cycle stops due to the timed schedule.

At 1pm the weekly hotter DHW cycle is started. At 13:29 the immersion briefly engages as the DHW has exceeded the 55°C DHW limit that is set for the heat pump.  This switches off 6 minutes later as the 58°C target has been reached.  Finally at 2pm this hotter DHW cycle ends and the DHW target temperature returns to 30°C.  Note the 'legionella' indicator was not set during this period as this unit does not use the disinfectant settings of the LG but rather uses the heat pump's standard DHW cycle with a higher target.

There is some interesting information even from this simple log.  The unit should be running silent mode except between 3 and 4 pm.  But what this log shows is that during the hotter DHW cycle silent mode was not active.  This appears to be a bug in the LG control software.  During the DHW cycle at 5:45 am silent mode is honoured, and we know that a heating cycle is also required at this time - but the DHW takes priority.  At 1pm there is no heating required - therefore silent mode is disabled for DHW only periods, i.e. where there isn't a corresponding heating mode.

### Emoncms Integration

With an emoncms account and the TVMG controller configured to send some data items to emoncms then the homeowner can chart these parameters in realtime.  The following chart encompasses the period that the previous data log.

<div style="display: flex; gap: 20px; align-items: flex-start; ">
  <img src="{{ '/docs/features/images/emon-feeds.jpg' | relative_url }}" style="max-height: 700px; width: auto;">
</div>

This helps to visualise the data from the log, and clearly shows how the system is responding.  Additional data, retrieved from the heat pump via modbus and from independent power meters helps to understand what is happening.  This unit is a series 4 so the LG's water flow rate can be monitored, and hence thermal power generation estimated.

This same data can be used by the emoncms heatpump app, also used by the installs showing public data from heat geeks etc at [heatpumpmonitor.org](https://heatpumpmonitor.org/){:target="_blank" rel="noopener noreferrer"}.  Remember that this is a thermal power estimation and is not the same as using a certified heat meter, but it is a useful guide.

<div style="display: flex; gap: 20px; align-items: flex-start; ">
  <img src="{{ '/docs/features/images/emon-app.jpg' | relative_url }}" style="max-height: 700px; width: auto;">
</div>



[LG Setup]({{ '/docs/install/lg-connection' | relative_url }})
