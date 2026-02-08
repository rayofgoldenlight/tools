using Microsoft.EntityFrameworkCore;

namespace Fpf.Data
{
    public class PatientRepository : IPatientRepository
    {
        private readonly AppDbContext _context;

        public PatientRepository(AppDbContext context)
        {
            _context = context;
        }

        public async Task<Patient?> GetByFhirIdAsync(string fhirId)
        {
            return await _context.Patients
                .FirstOrDefaultAsync(p => p.FhirId == fhirId);
        }

        public async Task AddOrUpdateAsync(Patient patient)
        {
            var existing = await GetByFhirIdAsync(patient.FhirId!);

            if (existing == null)
            {
                _context.Patients.Add(patient);
            }
            else
            {
                // update entity fields
                existing.GivenName = patient.GivenName;
                existing.FamilyName = patient.FamilyName;
                existing.BirthDate = patient.BirthDate;
                existing.Gender = patient.Gender;
                _context.Patients.Update(existing);
            }

            await _context.SaveChangesAsync();
        }

        public async Task<List<Patient>> GetFavoritesAsync(string username)
        {
            return await _context.Favorites
                .Where(f => f.Username == username)
                .Include(f => f.Patient)
                .Select(f => f.Patient!)
                .ToListAsync();
        }

        public async Task AddFavoriteAsync(int patientId, string username)
        {
            var already = await _context.Favorites
                .FirstOrDefaultAsync(f => f.PatientId == patientId && f.Username == username);

            if (already == null)
            {
                _context.Favorites.Add(new Favorite
                {
                    PatientId = patientId,
                    Username = username
                });
                await _context.SaveChangesAsync();
            }
        }

        public async Task RemoveFavoriteAsync(int patientId, string username)
        {
            var fav = await _context.Favorites
                .FirstOrDefaultAsync(f => f.PatientId == patientId && f.Username == username);

            if (fav != null)
            {
                _context.Favorites.Remove(fav);
                await _context.SaveChangesAsync();
            }
        }

        public async Task LogActionAsync(string actionType, int? patientId, string? username)
        {
            var log = new AuditLog
            {
                ActionType = actionType,
                PatientId = patientId,
                Username = username,
                Timestamp = DateTime.UtcNow
            };

            _context.AuditLogs.Add(log);
            await _context.SaveChangesAsync();
        }
    }
}