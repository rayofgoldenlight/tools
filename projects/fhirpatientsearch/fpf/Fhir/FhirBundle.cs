namespace Fpf.Fhir
{
    public class FhirBundle
    {
        public string? ResourceType { get; set; }
        public int? Total { get; set; }
        public List<FhirEntry>? Entry { get; set; }
    }
}