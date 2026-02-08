namespace Fpf.Fhir
{
    public class FhirPatient
    {
        public string? ResourceType { get; set; }
        public string? Id { get; set; }
        public List<FhirName>? Name { get; set; }
        public string? Gender { get; set; }
        public string? BirthDate { get; set; }
    }
}