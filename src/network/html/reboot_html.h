const char reboot_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html lang="en">
 <head>
  <title>Rebooting</title>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
   * { box-sizing: border-box; }
   body { background-color: #f7f7f7; font-family: system-ui, sans-serif; font-size: 13px; line-height: 1.2; color: #333; margin: 0; padding: 10px; }
   .container { max-width: 400px; margin: 100px auto; text-align: center; }
   fieldset { background-color: #fff; border: 1px solid #ccc; border-radius: 4px; padding: 25px; width: 100%%; }

   h2 { margin: 0 0 10px 0; font-size: 1.3em; color: #444; }
   p { color: #666; margin: 5px 0; }

   /* Hardened Spinner CSS */
   .spinner {
    border: 2px solid #f3f3f3;
    border-top: 2px solid #4CAF50;
    border-radius: 50%%; /* Double percent for C++ string safety */
    width: 14px;
    height: 14px;
    display: inline-block;
    vertical-align: middle;
    margin-left: 8px;
    -webkit-animation: spin 1s linear infinite;
    animation: spin 1s linear infinite;
   }

   @-webkit-keyframes spin { 0%% { -webkit-transform: rotate(0deg); } 100%% { -webkit-transform: rotate(360deg); } }
   @keyframes spin { 0%% { transform: rotate(0deg); } 100%% { transform: rotate(360deg); } }

   #status {
    font-weight: bold;
    color: #4CAF50;
    margin-top: 15px;
    display: flex;
    align-items: center;
    justify-content: center;
   }
  </style>

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
