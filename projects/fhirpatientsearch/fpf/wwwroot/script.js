document.addEventListener("DOMContentLoaded", () => {
  const searchBtn = document.getElementById("searchBtn");
  const searchInput = document.getElementById("searchName");
  const resultsContainer = document.getElementById("resultsContainer");
  const favoritesBtn = document.getElementById("favoritesBtn");

  // Utility to render results table
  function renderResults(patients) {
        if (!patients || patients.length === 0) {
            resultsContainer.innerHTML = `<p class="text-muted">No patients found.</p>`;
            return;
        }

        let tableHtml = `
            <table class="table table-striped results-table">
            <thead>
                <tr>
                <th>FHIR ID</th>
                <th>Given Name</th>
                <th>Family Name</th>
                <th>Gender</th>
                <th>Birth Date</th>
                <th></th>
                </tr>
            </thead>
            <tbody>
        `;

        for (const p of patients) {
            const isFavorite = p.patientId != null; // favorites from DB have this field

            tableHtml += `
            <tr>
                <td>${p.fhirId ?? ""}</td>
                <td>${p.givenName ?? ""}</td>
                <td>${p.familyName ?? ""}</td>
                <td>${p.gender ?? ""}</td>
                <td>${p.birthDate ?? ""}</td>
                <td>
                ${
                    isFavorite
                    ? `<button class="btn btn-sm btn-outline-danger remove-favorite-btn" 
                                data-patient-id="${p.patientId}">
                        🗑 Remove
                        </button>`
                    : `<button class="btn btn-sm btn-outline-success add-favorite-btn" 
                                data-fhir-id="${p.fhirId}">
                        ★ Add Favorite
                        </button>`
                }
                </td>
            </tr>
            `;
        }

        tableHtml += `</tbody></table>`;
        resultsContainer.innerHTML = tableHtml;

        // Wire up button handlers
        document.querySelectorAll(".add-favorite-btn").forEach(btn => {
            btn.addEventListener("click", () => addFavorite(btn));
        });
        document.querySelectorAll(".remove-favorite-btn").forEach(btn => {
            btn.addEventListener("click", () => removeFavorite(btn));
        });

        resultsContainer.scrollIntoView({ behavior: "smooth" });
        }

  // Fetch from backend API
  async function searchPatients(event) {
  event?.preventDefault(); // prevent full reload
  const loading = document.getElementById("loadingIndicator");
  const name = searchInput.value.trim();

  // Bootstrap form validation styling
  if (!name) {
    searchInput.classList.add("is-invalid");
    return;
  } else {
    searchInput.classList.remove("is-invalid");
  }

  // Show spinner
  loading.style.display = "block";
  resultsContainer.innerHTML = "";

  try {
    const response = await fetch(`/api/patients/search?name=${encodeURIComponent(name)}`);
    if (!response.ok)
      throw new Error(`Server returned ${response.status}`);

    const data = await response.json();
    renderResults(data);
    resultsContainer.scrollIntoView({ behavior: "smooth" });
  } catch (err) {
    console.error("Search error:", err);
    resultsContainer.innerHTML =
      `<p class="text-danger">Error fetching patients. Please try again.</p>`;
  } finally {
    loading.style.display = "none";
  }
}

  // Add patient to favorites
    // Add patient to favorites
async function addFavorite(button) {
        const row = button.closest("tr");
        const cells = row.querySelectorAll("td");
        const patient = {
            fhirId: cells[0].textContent.trim(),
            givenName: cells[1].textContent.trim(),
            familyName: cells[2].textContent.trim(),
            gender: cells[3].textContent.trim(),
            birthDate: cells[4].textContent.trim()
        };

        try {
            const response = await fetch("/api/patients/favorites", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(patient)
            });

            if (!response.ok) {
            const text = await response.text();
            throw new Error(text);
            }

            const msg = await response.json();
            alert(msg.message || "Favorite added!");
            row.classList.add("table-success");
            setTimeout(() => row.classList.remove("table-success"), 1000);
        } catch (err) {
            console.error("Favorite add error:", err);
            alert("Error adding favorite – see console.");
        }
        }

        // 👉 Move this OUTSIDE addFavorite
        async function removeFavorite(button) {
        const patientId = button.getAttribute("data-patient-id");
        if (!patientId) return;

        if (!confirm("Remove this favorite?")) return;

        try {
            const response = await fetch(`/api/patients/favorites/${patientId}`, {
            method: "DELETE"
            });

            if (!response.ok) throw new Error(`DELETE failed: ${response.status}`);

            const msg = await response.json();
            alert(msg.message || "Favorite removed.");

            // Remove row visually
            const row = button.closest("tr");
            row.classList.add("table-danger");
            setTimeout(() => row.remove(), 500);
        } catch (err) {
            console.error("Favorite remove error:", err);
            alert("Error removing favorite – see console.");
        }
    }

    // Fetch and display saved favorites
    async function loadFavorites() {
    try {
        const response = await fetch("/api/patients/favorites");
        if (!response.ok) throw new Error("Favorites GET failed");

        const favs = await response.json();
        renderResults(favs);
    } catch (err) {
        console.error(err);
        alert("Error loading favorites.");
    }
    }


  searchBtn.addEventListener("click", searchPatients);
    document.getElementById("searchForm").addEventListener("submit", searchPatients);
    favoritesBtn.addEventListener("click", loadFavorites);
});