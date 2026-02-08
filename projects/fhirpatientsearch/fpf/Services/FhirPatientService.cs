using System.Net.Http;
using System.Net.Http.Json;
using Fpf.Fhir;
using Microsoft.Extensions.Logging;

namespace Fpf.Services
{
    public interface IFhirPatientService
    {
        Task<List<SimplifiedPatient>> SearchPatientsAsync(string name);
    }

    public class FhirPatientService : IFhirPatientService
    {
        private readonly HttpClient _httpClient;
        private readonly ILogger<FhirPatientService> _logger;

        public FhirPatientService(IHttpClientFactory httpClientFactory, ILogger<FhirPatientService> logger)
        {
            _httpClient = httpClientFactory.CreateClient("fhir");
            _logger = logger;
        }

        public async Task<List<SimplifiedPatient>> SearchPatientsAsync(string name)
        {
            var results = new List<SimplifiedPatient>();
            var url = $"Patient?name={Uri.EscapeDataString(name)}&_count=10";

            _logger.LogInformation("FHIR Query [{Time}]: {Url}", DateTime.UtcNow, url);

            try
            {
                var response = await _httpClient.GetAsync(url);

                if (!response.IsSuccessStatusCode)
                {
                    // Log warning without throwing
                    _logger.LogWarning("FHIR server response {Code} for query {Url}", 
                                        (int)response.StatusCode, url);
                    return results;
                }

                var bundle = await response.Content.ReadFromJsonAsync<FhirBundle>();

                // Defensive check
                if (bundle?.Entry == null || bundle.Entry.Count == 0)
                {
                    _logger.LogInformation("No FHIR patients found for '{Name}'", name);
                    return results;
                }

                foreach (var entry in bundle.Entry)
                {
                    var p = entry.Resource;
                    if (p == null) continue;

                    results.Add(new SimplifiedPatient
                    {
                        FhirId = p.Id,
                        GivenName = p.Name?.FirstOrDefault()?.Given?.FirstOrDefault(),
                        FamilyName = p.Name?.FirstOrDefault()?.Family,
                        Gender = p.Gender,
                        BirthDate = p.BirthDate
                    });
                }

                _logger.LogInformation("FHIR '{Name}' => {Count} results", name, results.Count);
            }
            catch (TaskCanceledException ex)
            {
                _logger.LogError(ex, "FHIR timeout for '{Name}' after {Timeout}s", 
                                name, _httpClient.Timeout.TotalSeconds);
            }
            catch (HttpRequestException ex)
            {
                _logger.LogError(ex, "HTTPS error connecting FHIR server for '{Name}'", name);
            }
            catch (Exception ex)
            {
                _logger.LogError(ex, "Unexpected error in FHIR search for '{Name}'", name);
            }

            return results;
        }
    }
}