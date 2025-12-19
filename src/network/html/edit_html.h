const char edit_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html lang="en">
 <head>
  <title>Editor - %EDIT_FILENAME%</title>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
   /* Shared Framework Styles */
   * { box-sizing: border-box; }
   body { background-color: #f7f7f7; font-family: system-ui, sans-serif; font-size: 13px; line-height: 1.2; color: #333; margin: 0; padding: 10px; }
   .container { max-width: 720px; margin: 0 auto; }
   h2 { margin: 6px 0; font-size: 1.2em; text-align: center; }

   fieldset { background-color: #fff; border: 1px solid #ccc; border-radius: 4px; margin-bottom: 8px; padding: 12px; }
   legend { font-weight: bold; padding: 0 4px; font-size: 0.85em; color: #666; }

   /* Editor Styles */
   textarea {
    width: 100%%; height: 380px; padding: 10px; border: 1px solid #ddd; border-radius: 4px;
    font-family: 'Courier New', monospace; font-size: 12px; background: #fafafa;
    resize: vertical; display: block; margin-bottom: 12px; line-height: 1.4;
   }

   .action-bar { display: flex; justify-content: space-between; align-items: center; gap: 10px; }
   .input-group { flex: 1; display: flex; align-items: center; }

   /* Standardized Inputs/Buttons */
   input[type="text"] { height: 26px; padding: 0 8px; border: 1px solid #ccc; border-radius: 3px; width: 100%%; }
   input[type="submit"], button { padding: 0 15px; height: 26px; cursor: pointer; border-radius: 3px; border: 1px solid #bbb; background: #f0f0f0; font-size: 11px; font-weight: 500; }
   input[type="submit"]:hover { background: #e5e5e5; }
   #cancel { background: #fff; margin-right: 5px; }

   .footer-status { text-align: center; margin-top: 10px; font-size: 11px; color: #777; border-top: 1px solid #eee; padding-top: 8px; }
  </style>
 </head>
 <body>
  <div class="container">
   <h2>File Editor</h2>

   <fieldset>
    <legend>Editing: %EDIT_FILENAME%</legend>
    <form action="/save" method="post" onsubmit="return confirm('Save changes to %EDIT_FILENAME%?')">

     <textarea name="edit_textarea" spellcheck="false">%TEXTAREA_CONTENT%</textarea>

     <div class="action-bar">
      <div class="input-group">
       %SAVE_PATH_INPUT%
      </div>
      <div>
       <button type="button" id="cancel" onclick="window.location.href='/manager';">Cancel</button>
       <input type="submit" value="Save File">
      </div>
     </div>
    </form>
   </fieldset>
  </div>
 </body>
</html>
)rawliteral";
