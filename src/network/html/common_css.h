const char common_css[] PROGMEM = R"rawliteral(
<style>
  /* Global Resets */
  * { box-sizing: border-box; }
  body { background-color: #f7f7f7; font-family: system-ui, -apple-system, sans-serif; font-size: 13px; line-height: 1.2; color: #333; margin: 0; padding: 10px; }
  .container { max-width: 850px; margin: 0 auto; }
  h2 { margin: 6px 0; font-size: 1.2em; text-align: center; }

  /* Containers */
  fieldset { background-color: #fff; border: 1px solid #ccc; border-radius: 4px; margin-bottom: 8px; padding: 12px; width: 100%%; }
  legend { font-weight: bold; padding: 0 4px; font-size: 0.85em; color: #666; }

  /* Tables */
  table { border-collapse: collapse; width: 100%%; margin-bottom: 2px; font-size: 0.9em; }
  td, th { border: 1px solid #eee; padding: 4px 8px; text-align: left; }
  tr:nth-child(even) { background-color: #fcfcfc; }

  /* Navigation & Form Rows */
  .header-nav { display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px; }
  .form-row { display: flex; justify-content: space-between; align-items: center; padding: 4px 0; gap: 15px; border-bottom: 1px solid #f0f0f0; min-height: 32px; }
  .form-row:last-child { border-bottom: none; }
  .form-label { flex: 0 0 auto; min-width: 120px; }
  form { margin: 0; display: inline-flex; align-items: center; gap: 8px; flex: 1; justify-content: flex-end; }

  /* Modern Inputs, Selects & Textarea */
  select, input[type="text"], textarea {
    font-size: 12px; padding: 0 8px; border-radius: 5px; border: 1px solid #d1d5db;
    background: #fafafa; height: 26px; color: #374151; vertical-align: middle;
  }
  select { background: linear-gradient(to bottom, #ffffff, #f9fafb); cursor: pointer; min-width: 160px; }
  textarea { height: 380px; padding: 8px; display: block; width: 100%%; line-height: 1.4; font-family: monospace; }

  /* Unified Modern Buttons */
  input[type="submit"], input[type="button"], button, .nav-btn, .file-input-label {
      display: inline-flex; align-items: center; justify-content: center;
      padding: 0 12px; height: 26px; cursor: pointer; border-radius: 5px;
      border: 1px solid #d1d5db; background: linear-gradient(to bottom, #ffffff, #f9fafb);
      color: #374151; font-size: 11px; font-weight: 600; text-decoration: none;
      white-space: nowrap; transition: all 0.2s ease; box-shadow: 0 1px 2px rgba(0,0,0,0.05);
  }
  input[type="submit"]:hover, .nav-btn:hover, .file-input-label:hover, select:hover {
      background: #f3f4f6; border-color: #9ca3af; color: #111827;
  }
  #update-btn, .primary-btn { background: linear-gradient(to bottom, #4CAF50, #45a049) !important; color: white !important; border-color: #3d8b40 !important; }
  input[type="file"] { width: 0.1px; height: 0.1px; opacity: 0; overflow: hidden; position: absolute; z-index: -1; }

  /* Progress Bar */
  .progress-wrapper { width: 100%%; background-color: #ddd; border-radius: 4px; margin: 10px 0; height: 20px; overflow: hidden; border: 1px solid #ccc; display: block; }
  .progress-bar { width: 0%%; height: 20px; background-color: #4CAF50 !important; text-align: center; color: white; line-height: 20px; font-size: 11px; transition: width 0.3s ease; display: block; }

  /* Event Log Grid */
  #log-target { display: flex; flex-direction: column-reverse; }
  .log-row {
      display: grid; grid-template-columns: 85px 70px 50px 90px 70px 90px 60px 1fr 45px;
      align-items: center; gap: 8px; padding: 6px 0; border-bottom: 1px solid #eee;
  }
  .mode-off   { background-color: #777777 !important; }
  .mode-heat  { background-color: #4CAF50 !important; }
  .mode-dhw   { background-color: #FF9800 !important; }
  .mode-ai    { background-color: #8E24AA !important; }
  .f { opacity: 0.2; font-style: normal; display: inline-block; margin: 0 1px; }
  .f-on { opacity: 1 !important; }
  .cp-on      { color: #E65100 !important; font-weight: bold; }
  .time { font-family: monospace; font-weight: bold; color: #111; }
  .mode-cell { font-size: 10px; font-weight: bold; text-align: center; border-radius: 3px; padding: 3px 0; color: white; text-transform: uppercase; }
  .val-unit { font-weight: 600; color: #111; font-family: monospace; }
  .label { display: block; font-size: 8px; color: #999; text-transform: uppercase; margin-bottom: 1px; }
  .err-active { color: #d32f2f; background: #ffebee; border-radius: 3px; text-align: center; font-weight: bold; }
  .err-none { color: #ddd; text-align: center; }

  /* Misc */
  .spinner { border: 2px solid #f3f3f3; border-top: 2px solid #4caf50; border-radius: 50%%; width: 14px; height: 14px; animation: spin 1s linear infinite; display: inline-block; vertical-align: middle; }
  @keyframes spin { 0%% { transform: rotate(0deg); } 100%% { transform: rotate(360deg); } }
  .loader { text-align: center; padding: 20px; color: #888; font-style: italic; }
  #last-upd { text-align: center; font-size: 9px; color: #999; margin-top: 8px; }

  @media (max-width: 800px) {
      .log-row { grid-template-columns: 75px 65px 80px 60px 80px 1fr 35px; }
      .outdoor-col, .cp-col { display: none; }
  }
</style>
)rawliteral";
