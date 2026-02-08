namespace Fpf.Data
{
    public class Patient
    {
        public int PatientId { get; set; }            // Primary key
        public string? FhirId { get; set; }
        public string? GivenName { get; set; }
        public string? FamilyName { get; set; }
        public string? Gender { get; set; }
        public DateTime? BirthDate { get; set; }

        // Navigation properties
        public ICollection<AuditLog>? AuditLogs { get; set; }
        public ICollection<Favorite>? Favorites { get; set; }
    }
}