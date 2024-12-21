const char manager_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
 <head>
  <title>ESP32 SPIFFS Manager</title>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
   body { background-color: #f7f7f7; }
   #submit { width:120px; }
   #edit_path { width:250px; }
   #delete_path { width:250px; }
   #download_path { width:250px; }
   #spacer_50 { height: 50px; }
   #spacer_20 { height: 20px; }
   table { background-color: #dddddd; border-collapse: collapse; width:650px; }
   td, th { border: 1px solid #dddddd; text-align: left; padding: 8px; }
   #first_td_th { width:400px; }
   tr:nth-child(even) { background-color: #ffffff; }
   fieldset { width:700px; background-color: #f7f7f7; }
   #reset_notice { color: #ff0000; }
  </style>
  <script>
   function validateFormUpdate() {
    var inputElement = document.getElementById('update');
    var files = inputElement.files;
    if(files.length==0)
    {
      alert("You have not chosen a file!");
      return false;
    }
    var value = inputElement.value;
    var dotIndex = value.lastIndexOf(".")+1;
    var valueExtension = value.substring(dotIndex);
    if(valueExtension != "bin")
    {
      alert("Incorrect file type!");
      return false;
    }
   }
   function validateFormUpload() {
    var inputElement = document.getElementById('upload_data');
    var files = inputElement.files;
    if(files.length==0)
    {
      alert("You have not chosen a file!");
      return false;
    }
   }
   function validateFileEdit() {
    var allowedExtensions = "%ALLOWED_EXTENSIONS_EDIT%";
    var editSelectValue = document.getElementById('edit_path').value;
    var dotIndex = editSelectValue.lastIndexOf(".")+1;
    var editSelectValueExtension = editSelectValue.substring(dotIndex);
    var extIndex = allowedExtensions.indexOf(editSelectValueExtension);

    if(editSelectValue == "choose"){
      alert("You have not chosen a file!");
      return false;
    }
    if(extIndex == -1){
      alert("Editing of this file type is not supported!");
      return false;
    }
   }
   function validateFileDelete(){
    var fileName = document.getElementById('delete_path').value;
    if(fileName == "choose"){
      alert("You have not chosen a file!");
      return false;
    }
   }
   function validateFileDownload(){
    var fileName = document.getElementById('download_path').value;
    if(fileName == "choose"){
      alert("You have not chosen a file!");
      return false;
    }
   }
   function confirmReset(){
    var text = "WARNING: Pressing the \"OK\" button immediately resets to defaults and restarts";
    if (confirm(text) == true){
      return true;
    }
    else{
      return false;
    }
   }
  </script>
 </head>
 <body>
   <center>
     <h2>ThermaV Monitor Firmware Update [current : %VERSION%]</h2>
     <div id="spacer_20"></div>
     <fieldset>
      <legend>Firmware Update</legend>
      <div id="spacer_20"></div>
      <form method="POST" action="/update" enctype="multipart/form-data">
       <table><tr><td id="first_td_th">
       <input type="file" id="update" name="update">
       </td><td>
       <input type="submit" id="submit" value="Update!" onclick="return validateFormUpdate()">
       </td></tr></table>
      </form>
      <div id="spacer_20"></div>
     </fieldset>

     <div id="spacer_20"></div>
     <h2>ESP32 SPIFFS Manager</h2>

     <div id="spacer_20"></div>
     <fieldset>
      <legend>File list</legend>
      <div id="spacer_20"></div>
      <p>Full SPIFFS storage: %SPIFFS_TOTAL_BYTES%, used: %SPIFFS_USED_BYTES%, available: %SPIFFS_FREE_BYTES%</p>
      <div id="spacer_20"></div>
      %LISTEN_FILES%
      <div id="spacer_20"></div>
     </fieldset>

     <div id="spacer_20"></div>
     <fieldset>
      <legend>File upload</legend>
      <div id="spacer_20"></div>
      <form method="POST" action="/upload" enctype="multipart/form-data">
       <table><tr><td id="first_td_th">
       <input type="file" id="upload_data" name="upload_data">
       </td><td>
       <input type="submit" id="submit" value="File upload!" onclick="return validateFormUpload()">
       </td></tr></table>
      </form>
      <div id="spacer_20"></div>
     </fieldset>

     <div id="spacer_20"></div>
     <fieldset>
      <legend>Edit file</legend>
      <div id="spacer_20"></div>
      <form method="GET" action="/edit">
       <table><tr><td id="first_td_th">
       %EDIT_FILES%
       </td><td>
       <input type="submit" id="submit" value="Edit" onclick="return validateFileEdit()">
       </td></tr></table>
      </form>
      <div id="spacer_20"></div>
     </fieldset>

     <div id="spacer_20"></div>
     <fieldset>
       <legend>Delete file</legend>
       <div id="spacer_20"></div>
       <form method="GET" action="/delete">
        <table><tr><td id="first_td_th">
        %DELETE_FILES%
        </td><td>
        <input type="submit" id="submit" value="Delete" onclick="return validateFileDelete()">
        </td></tr></table>
       </form>
       <div id="spacer_20"></div>
     </fieldset>

     <div id="spacer_20"></div>
     <fieldset>
      <legend>Download file</legend>
      <div id="spacer_20"></div>
      <form method="GET" action="/download">
        <table><tr><td id="first_td_th">
        %DOWNLOAD_FILES%
        </td><td>
        <input type="submit" id="download" value="Download" onclick="return validateFileDownload()">
        </td></tr></table>
      </form>
      <div id="spacer_20"></div>
     </fieldset>

     <div id="spacer_20"></div>
     <fieldset>
      <legend>Reset Board</legend>
      <div id="spacer_20"></div>
      <form method="POST" action="/reset" target="self_page">
        <table><tr><td id="first_td_th">
        <p id="reset_notice">Pressing the 'Reset' button will reset the board !</p>
        </td><td>
        <input type="submit" id="submit" value="Reset" onclick="return confirmReset()">
        </td></tr></table>
      </form>
      <div id="spacer_20"></div>
     </fieldset>

     <div id="spacer_20"></div>
     <fieldset>
      <legend>Reboot</legend>
      <div id="spacer_20"></div>
      <form method="POST" action="/reboot" target="self_page">
        <table><tr><td id="first_td_th">
        <p>Soft Reboot the device</p>
        </td><td>
        <input type="submit" id="submit" value="Reboot">
        </td></tr></table>
      </form>
      <div id="spacer_20"></div>
     </fieldset>

     <div id="spacer_50"></div>
     <iframe style="display:none" name="self_page"></iframe>
   </center>
 </body>
</html> )rawliteral";
