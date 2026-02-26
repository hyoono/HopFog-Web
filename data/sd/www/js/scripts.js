window.addEventListener('DOMContentLoaded', event => {

    // Toggle the side navigation
    const sidebarToggle = document.body.querySelector('#sidebarToggle');
    if (sidebarToggle) {
        sidebarToggle.addEventListener('click', event => {
            event.preventDefault();
            document.body.classList.toggle('sb-sidenav-toggled');
            localStorage.setItem('sb|sidebar-toggle', document.body.classList.contains('sb-sidenav-toggled'));
        });
    }

    // Load current user's name into the sidebar (runs on every page)
    const sidebarUser = document.getElementById('sidebarUsername');
    if (sidebarUser) {
        fetch('/api/dashboard')
            .then(function(r) { return r.json(); })
            .then(function(data) {
                if (data.current_user && data.current_user.username) {
                    sidebarUser.textContent = data.current_user.username;
                }
            })
            .catch(function() { /* keep default text */ });
    }

    // Sync browser clock to ESP32 (ESP32 has no NTP in AP mode)
    fetch('/status').then(function(r) { return r.json(); }).then(function(data) {
        if (data.online) {
            var now = Math.floor(Date.now() / 1000);
            fetch('/api/set-time', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ epoch: now })
            }).catch(function() {});
        }
    }).catch(function() {});

});
