/*
 * Copyright (c) 2026, Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

const char credits_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html lang="en">
 <head>
  <title>Credits & Licensing</title>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  %STYLE%
  <style>
    .license-text { font-family: monospace; font-size: 11px; white-space: pre-wrap; line-height: 1.4; color: #444; }
    .disclaimer { font-weight: bold; color: #000; text-transform: uppercase; border-top: 1px solid #eee; margin-top: 10px; padding-top: 10px; }
    .oss-link { color: #4CAF50; text-decoration: none; font-weight: bold; }
  </style>
 </head>
 <body>
  <div class="container">
    <div class="header-nav">
      <a href="/manager" class="nav-btn">&larr; Manager</a>
      <h2>TVMG Open Source Information</h2>
      <div style="width:85px"></div>
   </div>
   <fieldset>
    <legend>Project License & Disclaimer</legend>
    <div class="license-text">
Copyright (c) 2026, Peter Walton
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

<div class="disclaimer">THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.</div>
    </div>
   </fieldset>

   <fieldset>
    <legend>Third-Party Components</legend>
    <table>
     <thead>
      <tr>
       <th>Component</th>
       <th>License</th>
       <th>Version</th>
      </tr>
     </thead>
     <tbody>
      <tr><td>arduino-esp32 core</td><td>LGPL 2.1 / Apache 2.0</td><td>3.1.0</td></tr>
      <tr><td>AsyncTCP</td><td>LGPL 3.0</td><td>3.3.1</td></tr>
      <tr><td>Dallas Temp. Controller</td><td>LGPL 2.1</td><td>3.9.0</td></tr>
      <tr><td>ESPASync Webserver</td><td>LGPL 3.0</td><td>3.4.5</td></tr>
      <tr><td>ModbusMaster</td><td>GPL 3.0 / Apache 2.0</td><td>2.0.1</td></tr>
      <tr><td>modbus-esp8266</td><td>BSD 3-Clause</td><td>4.1.0</td></tr>
      <tr><td>OneWire</td><td>Unique</td><td>2.3.8</td></tr>
      <tr><td>ReadyMail</td><td>MIT</td><td>0.3.6</td></tr>
      %GFX_LIBRARY%
     </tbody>
    </table>
   </fieldset>

   <fieldset style="text-align: center; padding: 15px;">
    <legend>Compliance & Source Code</legend>
    In compliance with LGPL 3.0, build tools and sources are available at:<br><br>
    <a class="oss-link" href="https://github.com/tvmg-dev/tvmg/tree/main/oss" target="_blank">github.com/tvmg-dev/tvmg/tree/main/oss</a>
   </fieldset>

   <div id="last-upd">Firmware %VERSION% &copy; 2026 Peter Walton</div>
  </div>
 </body>
</html>
)rawliteral";