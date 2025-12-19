const char manager_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
 <head>
  <title>TMVG Manager</title>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta http-equiv="refresh" content="120">
  <style>
   body { background-color: #f7f7f7; }
   #submit { width:120px; }
   #edit_path { width:250px; }
   #delete_path { width:250px; }
   #download_path { width:250px; }
   #spacer_10 { height: 10px; }
   #spacer_5 { height: 5px; }
   #first_td_th { width:400px; }
   #reset_notice { color: #ff0000; }
   table { background-color: #dddddd; border-collapse: collapse; width:650px; }
   td, th { border: 1px solid #dddddd; text-align: left; padding: 8px; }
   tr:nth-child(even) { background-color: #ffffff; }
   fieldset { width:700px; background-color: #f7f7f7; }
   p {margin-bottom: 0em;  margin-top: 0em; }
   .left { display:inline-block; float: left; text-align:left; margin-left: 30px; }

   /* Progress Bar Styles - using %% to escape for the async webserver processor */
   .progress-wrapper { width: 100%%; background-color: #ddd; border-radius: 5px; margin: 10px 0; display:none; }
   .progress-bar { width: 0%%; height: 20px; background-color: #4CAF50; border-radius: 5px; text-align: center; color: white; line-height: 20px; transition: width 0.3s; }
   .spinner { border: 4px solid #f3f3f3; border-top: 4px solid #3498db; border-radius: 50%%; width: 18px; height: 18px; animation: spin 1s linear infinite; display: inline-block; vertical-align: middle; margin-left: 10px; }
   @keyframes spin { 0%% { transform: rotate(0deg); } 100%% { transform: rotate(360deg); } }
  </style>

  <script>
   /* AJAX OTA Logic */
   function startOTAUpdate() {
    var input = document.getElementById('update');
    var file = input.files[0];
    if(input.files.length==0) { alert("You have not chosen a file!"); return; }
    if(!file.name.endsWith(".bin")) { alert("Incorrect file type!"); return; }

    document.getElementById('ota_form').style.display = 'none';
    document.getElementById('ota_progress_ui').style.display = 'block';

    var bar = document.getElementById('ota_bar');
    var status = document.getElementById('ota_status');
    var source = new EventSource('/events');

    source.addEventListener('ota_progress', function(e) {
     var bar = document.getElementById('ota_bar');
     var progress = parseInt(e.data);
     bar.style.width = progress + '%%';
     bar.innerHTML = progress + '%%';
    }, false);

    source.addEventListener('ota_state', function(e) {
     if (e.data === "reboot") {
      source.close();
      status.innerHTML = "<b>Update Successful: Rebooting...</b> <div class='spinner'></div>";
      setTimeout(function() { window.location.href = "/manager"; }, 10000);
     } else if (e.data.startsWith("failed")) {
      source.close();
      alert("Update Failed: " + e.data.split(":")[1]);
      location.reload();
     }
    }, false);

    var xhr = new XMLHttpRequest();
    var formData = new FormData();
    formData.append("update", file);
    xhr.open("POST", "/update", true);
    xhr.send(formData);
   }

   function validateFormUpdate() {
    var inputElement = document.getElementById('update');
    if(inputElement.files.length==0) { alert("You have not chosen a file!"); return false; }
    if(!inputElement.value.endsWith(".bin")) { alert("Incorrect file type!"); return false; }
    return true;
   }
   function validateFormUpload() {
    var inputElement = document.getElementById('upload_data');
    if(inputElement.files.length==0) { alert("You have not chosen a file!"); return false; }
   }
   function validateFileEdit() {
    var allowedExtensions = "%ALLOWED_EXTENSIONS_EDIT%";
    var editSelectValue = document.getElementById('edit_path').value;
    if(editSelectValue == "choose"){ alert("You have not chosen a file!"); return false; }
    if(allowedExtensions.indexOf(editSelectValue.substring(editSelectValue.lastIndexOf(".")+1)) == -1){ alert("Editing of this file type is not supported!"); return false; }
   }
   function validateFileDelete(){
    var fileName = document.getElementById('delete_path').value;
    if(fileName == "choose"){ alert("You have not chosen a file!"); return false; }
    return confirm("WARNING: Pressing the \"OK\" button will delete " + fileName);
   }
   function validateFileDownload(){
    if(document.getElementById('download_path').value == "choose"){ alert("You have not chosen a file!"); return false; }
   }
   function confirmReset(){
    return confirm("WARNING: Pressing the \"OK\" button immediately resets to defaults and restarts");
   }
   function checkbox(element){
    var xhr = new XMLHttpRequest();
    xhr.open("GET","/checkbox?item="+element.id+"&state="+(element.checked?"1":"0"),true);
    xhr.send();
   }
  </script>
 </head>

 <body>
   <center>
     <h2>ThermaV Monitor</h2>
     <div id="spacer_5"></div>
     <fieldset><legend>Firmware Update</legend>
        <table><tbody><tr>
         <td colspan="2">Current Version : %VERSION%</td>
         <td colspan="2">%UPTIME%</td></tr>
         <tr>
          <td width="25%%">%IPADDR%</td>
          <td width="25%%">%WIFI%</td>
          <td width="25%%">%MODBUS%</td>
          <td width="25%%">%EMON%</td>
         </tr>
        </tbody></table>
      <div id="spacer_5"></div>

      <div id="ota_form">
        <table><tr><td id="tdth1">
        <input type="file" id="update" name="update">
        </td><td>
        <input type="button" id="submit" value="Update!" onclick="startOTAUpdate()">
        </td></tr></table>
      </div>

      <div id="ota_progress_ui" style="display:none;">
        <p id="ota_status">Uploading & Flashing...</p>
        <div class="progress-wrapper" style="display:block;"><div id="ota_bar" class="progress-bar">0%%</div></div>
      </div>

      <div id="spacer_5"></div>
     </fieldset>

     <div id="spacer_5"></div>
     <h2>ESP32 SPIFFS Manager</h2>

     <div id="spacer_5"></div>
     <fieldset><legend>File list</legend>
      <p>Full SPIFFS storage: %SPIFFS_TOTAL_BYTES%, used: %SPIFFS_USED_BYTES%, available: %SPIFFS_FREE_BYTES%</p>
      <div id="spacer_5"></div>
      %LISTEN_FILES%
      <div id="spacer_5"></div>
     </fieldset>

     <div id="spacer_5"></div>
     <fieldset><legend>File upload</legend>
      <div id="spacer_5"></div>
      <form method="POST" action="/upload" enctype="multipart/form-data">
       <table><tr><td id="tdth2">
       <input type="file" id="upload_data" name="upload_data">
       </td><td>
       <input type="submit" id="submit" value="File upload!" onclick="return validateFormUpload()">
       </td></tr></table>
      </form>
      <div id="spacer_5"></div>
     </fieldset>

     <div id="spacer_5"></div>
     <fieldset><legend>Edit file</legend>
      <div id="spacer_5"></div>
      <form method="GET" action="/edit">
       <table><tr><td id="tdth3">
       %EDIT_FILES%
       </td><td>
       <input type="submit" id="submit" value="Edit" onclick="return validateFileEdit()">
       </td></tr></table>
      </form>
      <div id="spacer_5"></div>
     </fieldset>

     <div id="spacer_5"></div>
     <fieldset><legend>Delete file</legend>
       <div id="spacer_5"></div>
       <form method="GET" action="/delete">
        <table><tr><td id="tdth4">
        %DELETE_FILES%
        </td><td>
        <input type="submit" id="submit" value="Delete" onclick="return validateFileDelete()">
        </td></tr></table>
       </form>
       <div id="spacer_5"></div>
     </fieldset>

     <div id="spacer_5"></div>
     <fieldset><legend>Download file</legend>
      <div id="spacer_5"></div>
      <form method="GET" action="/download">
        <table><tr><td id="tdth5">
        %DOWNLOAD_FILES%
        </td><td>
        <input type="submit" id="download" value="Download" onclick="return validateFileDownload()">
        </td></tr></table>
      </form>
      <div id="spacer_5"></div>
     </fieldset>

     <div id="spacer_5"></div>
     <fieldset><legend>Options</legend>
      <div id="spacer_5"></div>
        <table><tr><td id="tdth16">
        <p>Soft Reboot the device</p>
        </td><td>
         <form method="POST" action="/reboot" target="_self">
         <input type="submit" id="submit" value="Reboot">
         </form>
        </td></tr>
        %OPTIONS_SECTION%
        </table>
      <div id="spacer_5"></div>
     </fieldset>

     <div id="spacer_5"></div>
     <fieldset><legend>Reset Board</legend>
      <div id="spacer_5"></div>
      <form method="POST" action="/reset" target="_self">
        <table><tr><td id="tdth7">
        <p id="reset_notice">Pressing the 'Reset' button will reset the board ! <br>
        (This will need additional confirmation) </p>
        </td><td>
        <input type="submit" id="submit" value="Reset" onclick="return confirmReset()">
        </td></tr></table>
      </form>
      <div id="spacer_5"></div>
     </fieldset>

     <div id="spacer_10"></div>
     <iframe style="display:none" name="self_page"></iframe>
   </center>
 </body>
</html>
)rawliteral";
