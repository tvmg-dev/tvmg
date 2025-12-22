const char edit_html[] = R"rawliteral(
<!DOCTYPE HTML>
<html lang="en">
 <head>
  <title>Editor - %EDIT_FILENAME%</title>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  %STYLE%
  <style>
    /* Specific override to prevent the unified 'form' flex from squashing the editor */
    .editor-container form {
        display: block;
        width: 100%%;
    }
    .editor-container textarea {
        width: 100%%;
        height: 450px;
        margin-bottom: 12px;
        display: block;
        font-family: 'Consolas', 'Monaco', monospace;
        line-height: 1.4;
        padding: 10px;
    }
    .editor-footer {
        display: flex;
        justify-content: space-between;
        align-items: center;
        gap: 10px;
    }
  </style>
 </head>
 <body>
  <div class="container editor-container">
   <h2>File Editor</h2>

   <fieldset>
    <legend>Editing: %EDIT_FILENAME%</legend>
    <form action="/save" method="post" onsubmit="return confirm('Save changes to %EDIT_FILENAME%?')">

     <textarea name="edit_textarea" spellcheck="false" wrap="off">%TEXTAREA_CONTENT%</textarea>

     <div class="editor-footer">
       <div style="display: flex; gap: 8px;">
         <button type="button" class="nav-btn" onclick="window.location.href='/manager';">Cancel</button>
         <input type="submit" value="Save File" class="primary-btn">
       </div>
     </div>

    </form>
   </fieldset>
  </div>
 </body>
</html>
)rawliteral";
