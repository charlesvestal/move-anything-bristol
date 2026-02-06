globalThis.init = function() { display.clear(); display.drawText(10, 28, "OB-Xa", 1); host_flush_display(); };
globalThis.tick = function() {};
globalThis.onMidiMessage = function(msg) { if (msg.length >= 1) host_module_send_midi(msg, 0); };
