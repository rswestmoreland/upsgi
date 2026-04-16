# A2 Cold connection churn asset pack

This scenario reuses the A1 browser mix PSGI app and request sequence but disables
keepalive and opens a fresh connection for every request. The measured runner drives
ramped target request rates to expose accept-path pressure without introducing
request-body or slow-reader variables.
