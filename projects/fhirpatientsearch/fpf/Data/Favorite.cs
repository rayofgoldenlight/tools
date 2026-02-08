namespace Fpf.Data
{
    public class Favorite
    {
        public int FavoriteId { get; set; }
        public string? Username { get; set; }

        // Foreign key
        public int PatientId { get; set; }
        public Patient? Patient { get; set; }
    }
}