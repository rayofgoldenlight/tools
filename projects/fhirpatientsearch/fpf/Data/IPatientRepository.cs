using Fpf.Data;

namespace Fpf.Data
{
    public interface IPatientRepository
    {
        Task<Patient?> GetByFhirIdAsync(string fhirId);
        Task AddOrUpdateAsync(Patient patient);
        Task<List<Patient>> GetFavoritesAsync(string username);
        Task AddFavoriteAsync(int patientId, string username);
        Task RemoveFavoriteAsync(int patientId, string username);
        Task LogActionAsync(string actionType, int? patientId, string? username);
    }
}