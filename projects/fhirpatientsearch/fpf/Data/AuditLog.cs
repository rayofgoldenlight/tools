namespace Fpf.Data
{
    public class AuditLog
    {
        public int AuditLogId { get; set; }
        public string? ActionType { get; set; }
        public DateTime Timestamp { get; set; } = DateTime.UtcNow;
        public string? Username { get; set; }

        // Foreign key
        public int? PatientId { get; set; }
        public Patient? Patient { get; set; }
    }
}