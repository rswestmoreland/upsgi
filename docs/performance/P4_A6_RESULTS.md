# P4-A6 Results

Focused measured slow-reader clients run using the accepted W4 async getline app.

Cases:
- 4 slow readers at 8 KiB/s
- 4 slow readers at 64 KiB/s
- 5 control GET /small requests during each slow-reader flood
- stream body size: 2 chunks x 65536 bytes = 131072 bytes per slow-reader response

Findings:
- control requests stayed fast and all succeeded in both cases
- slow readers completed successfully in both cases
- listen queue stayed at 0 in sampled stats
- this supports the conclusion that the accepted W4 PSGI slow-writer redesign is doing its job on the response side
