const char reboot_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Rebooting...</title>
  <meta http-equiv="refresh" content="15; URL=/manager">
  <style>
    body { font-family: sans-serif; text-align: center; margin-top: 50px; }
    .spinner {
      border: 8px solid #f3f3f3; /* Light grey */
      border-top: 8px solid #3498db; /* Blue */
      border-radius: 50%;
      width: 60px;
      height: 60px;
      animation: spin 2s linear infinite;
      margin: 20px auto;
    }
    @keyframes spin {
      0% { transform: rotate(0deg); }
      100% { transform: rotate(360deg); }
    }
  </style>
</head>
<body>
  <h1>Rebooting...</h1>
  <div class="spinner"></div>
  <p>The device is rebooting. This may take a few moments.</p>
  <p>Please wait while you are redirected to the homepage.</p>
</body>
</html>)rawliteral";
