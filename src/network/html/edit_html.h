/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

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
    <form action="/save" method="post" onsubmit="return validateAndConfirm()">

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
  <script>
  window.onload = function() {
    const ta = document.querySelector('textarea[name="edit_textarea"]');
    try {
        // Attempt to parse and re-stringfy with 2-space indentation
        const obj = JSON.parse(ta.value);
        ta.value = JSON.stringify(obj, null, 2);
    } catch (e) {
        // Not valid JSON or already formatted, leave as is
        console.log("Not a JSON file, skipping auto-format.");
    }
  };
  function validateAndConfirm() {
    const ta = document.querySelector('textarea[name="edit_textarea"]');
    const fileName = "%EDIT_FILENAME%";

    // 1. If it's a JSON file, check syntax first
    if (fileName.endsWith(".json")) {
        try {
            const obj = JSON.parse(ta.value);
            // Optional: Minify here to save space on the ESP32
            ta.value = JSON.stringify(obj); 
        } catch (e) {
            alert("JSON Error: " + e.message + "\n\nFile not saved. Please fix the syntax.");
            return false; // Stop everything
        }
    }

    // 2. Syntax is valid (or it's not a JSON file), now ask for confirmation
    return confirm('Save changes to ' + fileName + '?');
  }
  </script>  
 </body>
</html>
)rawliteral";
