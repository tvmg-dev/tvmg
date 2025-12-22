const char reboot_html[]  = R"rawliteral(
<!DOCTYPE HTML>
<html lang="en">
 <head>
  <title>Rebooting</title>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  %STYLE%

  <script>
   function pingServer() {
     fetch('/manager', { mode: 'no-cors' })
      .then(() => {
        document.getElementById('status').innerHTML = "System online! Redirecting...";
        setTimeout(() => { window.location.href = "/manager"; }, 1000);
      })
      .catch(() => { setTimeout(pingServer, 2000); });
   }
   window.onload = function() { setTimeout(pingServer, 5000); };
  </script>
 </head>
 <body>
  <div class="container">
   <fieldset>
    <h2>Rebooting</h2>
    <p>The device is restarting to apply changes.</p>
    <div id="status">
     Connecting to device <div class="spinner"></div>
    </div>
    <div style="display:none">%VERSION%</div>
   </fieldset>
  </div>
 </body>
</html>
)rawliteral";
