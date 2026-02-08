using Fpf.Config;
using Fpf.Data;
using Microsoft.EntityFrameworkCore;
using Fpf.Services;
using Fpf.Fhir;

static IResult ValidationError(string message) =>
    Results.BadRequest(new { error = message });

var builder = WebApplication.CreateBuilder(args);

builder.Logging.ClearProviders();
builder.Logging.AddConsole();

// Bind "FhirSettings" section from appsettings.json to the FhirSettings class
builder.Services.Configure<FhirSettings>(
    builder.Configuration.GetSection("FhirSettings"));

// Register HttpClient for FHIR calls (typed or basic)
builder.Services.AddHttpClient("fhir", client =>
{
    var baseUrl = builder.Configuration["FhirSettings:BaseUrl"];
    client.BaseAddress = new Uri(baseUrl!);
});

builder.Services.AddDbContext<AppDbContext>(options =>
    options.UseSqlServer(
        builder.Configuration.GetConnectionString("DefaultConnection")));

builder.Services.AddScoped<IPatientRepository, PatientRepository>();

builder.Services.AddScoped<IFhirPatientService, FhirPatientService>();

var app = builder.Build();

app.UseHttpsRedirection();
app.UseStaticFiles();


// ------------------------------------------------------------------------------------------
// PATIENT ENDPOINTS
// ------------------------------------------------------------------------------------------

app.MapGet("/api/patients/search", async (
    string? name,
    IFhirPatientService fhirService,
    IPatientRepository repo,
    ILogger<Program> logger) =>
{
    if (string.IsNullOrWhiteSpace(name))
        return ValidationError("Query parameter 'name' is required.");

    // Optional: reject too-short names
    if (name.Length < 2)
        return ValidationError("Search name must be at least two characters.");

    logger.LogInformation("Received patient search for: {Name}", name);

    var results = await fhirService.SearchPatientsAsync(name);

    // Log & return 
    await repo.LogActionAsync("FHIR Search", null, "System");
    return Results.Ok(results);
})
.WithTags("Patients");

// ------------------------------------------------------------------------------------------
// FAVORITES ENDPOINTS
// ------------------------------------------------------------------------------------------

app.MapGet("/api/patients/favorites", async (IPatientRepository repo) =>
{
    var favorites = await repo.GetFavoritesAsync("System");
    return Results.Ok(favorites);
})
.WithTags("Favorites");

app.MapPost("/api/patients/favorites", async (
    SimplifiedPatient patient,
    IPatientRepository repo,
    ILogger<Program> logger,
    AppDbContext db) =>
{
    if (string.IsNullOrWhiteSpace(patient.FhirId))
        return ValidationError("FHIR Id is required.");

    // Check if the patient record already exists in the DB
    var existingPatient = await db.Patients
        .FirstOrDefaultAsync(p => p.FhirId == patient.FhirId);

    // If not, create it again
    if (existingPatient == null)
    {
        existingPatient = new Patient
        {
            FhirId = patient.FhirId,
            GivenName = patient.GivenName,
            FamilyName = patient.FamilyName,
            Gender = patient.Gender,
            BirthDate = DateTime.TryParse(patient.BirthDate, out var d) ? d : null
        };
        db.Patients.Add(existingPatient);
        await db.SaveChangesAsync();
    }

    // Now safely add the favorite
    var existingFavs = await repo.GetFavoritesAsync("System");
    if (existingFavs.Any(f => f.PatientId == existingPatient.PatientId))
        return Results.Ok(new { message = "Patient already in favorites." });

    await repo.AddFavoriteAsync(existingPatient.PatientId, "System");
    await repo.LogActionAsync("FavoriteAdded", existingPatient.PatientId, "System");

    logger.LogInformation("Favorite added for FHIR {FhirId}", patient.FhirId);
    return Results.Ok(new { message = "Favorite added.", patientId = existingPatient.PatientId });
});

// DELETE /api/patients/favorites/clear  → remove all favorites for current user
app.MapDelete("/api/patients/favorites/clear", async (IPatientRepository repo, ILogger<Program> logger) =>
{
    var favorites = await repo.GetFavoritesAsync("System");

    if (!favorites.Any())
        return Results.Ok(new { message = "No favorites to clear." });

    foreach (var f in favorites.ToList())
    {
        await repo.RemoveFavoriteAsync(f.PatientId, "System");
        await repo.LogActionAsync("FavoriteRemoved", f.PatientId, "System");
    }

    logger.LogInformation("Cleared {Count} favorites for System user.", favorites.Count);
    return Results.Ok(new { message = $"{favorites.Count} favorites removed." });
})
.WithTags("Favorites")
.WithName("ClearAllFavorites");


app.MapDelete("/api/patients/favorites/{patientId:int}", async (int patientId, IPatientRepository repo) =>
{
    var favorites = await repo.GetFavoritesAsync("System");
    if (!favorites.Any(f => f.PatientId == patientId))
        return ValidationError($"No favorite found for PatientId={patientId}");

    await repo.RemoveFavoriteAsync(patientId, "System");
    await repo.LogActionAsync("FavoriteRemoved", patientId, "System");
    return Results.Ok(new { message = "Favorite removed" });
})
.WithTags("Favorites");

// ------------------------------------------------------------------------------------------
// AUDIT ENDPOINTS
// ------------------------------------------------------------------------------------------

app.MapGet("/api/audit/recent", async (
    int? limit,
    AppDbContext db) =>
{
    int take = limit is > 0 and <= 100 ? limit.Value : 20;

    var recent = await db.AuditLogs
        .OrderByDescending(a => a.Timestamp)
        .Take(take)
        .ToListAsync();

    return Results.Ok(recent);
})
.WithTags("Audit")
.WithName("GetRecentAudits");

app.MapGet("/api/audit/by-action", async (
    string? action,
    AppDbContext db) =>
{
    if (string.IsNullOrWhiteSpace(action))
        return ValidationError("Query param 'action' is required.");

    var logs = await db.AuditLogs
        .Where(a => a.ActionType == action)
        .OrderByDescending(a => a.Timestamp)
        .Take(50)
        .ToListAsync();

    return Results.Ok(logs);
})
.WithTags("Audit")
.WithName("GetAuditByAction");

app.MapGet("/api/audit/by-dates", async (
    string? start,
    string? end,
    AppDbContext db) =>
{
    if (!DateTime.TryParse(start, out var startDate))
        startDate = DateTime.UtcNow.AddDays(-7); // default last 7 days

    DateTime? endDate = null;
    if (DateTime.TryParse(end, out var parsedEnd))
        endDate = parsedEnd;

    var query = db.AuditLogs.AsQueryable();

    query = query.Where(a => a.Timestamp >= startDate);
    if (endDate.HasValue)
        query = query.Where(a => a.Timestamp <= endDate.Value);

    var logs = await query
        .OrderByDescending(a => a.Timestamp)
        .Take(200)
        .ToListAsync();

    return Results.Ok(new
    {
        from = startDate,
        to = endDate ?? DateTime.UtcNow,
        count = logs.Count,
        logs
    });
})
.WithTags("Audit")
.WithName("GetAuditByDates");

// ------------------------------------------------------------------------------------------
// FALLBACK
// ------------------------------------------------------------------------------------------

app.MapFallbackToFile("index.html");

/*SQL Table Test:
app.MapGet("/testdb", async (IPatientRepository repo) =>
{
    await repo.LogActionAsync("TestRun", null, "System");
    return Results.Ok("Database write succeeded");
});

app.MapGet("/auditlogs", async (AppDbContext db) =>
{
    var logs = await db.AuditLogs
                      .OrderByDescending(a => a.Timestamp)
                      .Take(5)
                      .ToListAsync();
    return Results.Ok(logs);
});


app.MapGet("/fhir-test", async (string name, IFhirPatientService fhirService) =>
{
    var patients = await fhirService.SearchPatientsAsync(name);
    return Results.Ok(patients);
});
*/

app.Run();

