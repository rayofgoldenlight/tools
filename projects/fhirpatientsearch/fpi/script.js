document.addEventListener("DOMContentLoaded", () => {
  const slides = document.querySelectorAll(".slide");
  let currentIndex = 0;
  let autoSlideInterval = null;
  let autoCycleStopped = false;

  const prevBtn = document.getElementById("prevBtn");
  const nextBtn = document.getElementById("nextBtn");

  function showSlide(index) {
    slides.forEach((s, i) => {
      s.classList.toggle("active", i === index);
    });
  }

  function nextSlide() {
    currentIndex = (currentIndex + 1) % slides.length;
    showSlide(currentIndex);
  }

  function prevSlide() {
    currentIndex = (currentIndex - 1 + slides.length) % slides.length;
    showSlide(currentIndex);
  }

  // 🔹 Auto-cycle every 5 seconds
  function startAutoCycle() {
    autoSlideInterval = setInterval(() => {
      if (!autoCycleStopped) nextSlide();
    }, 5000);
  }

  // 🔹 Stop the automatic cycling entirely after user interaction
  function stopAutoCycle() {
    autoCycleStopped = true;
    if (autoSlideInterval) {
      clearInterval(autoSlideInterval);
      autoSlideInterval = null;
    }
  }

  // Wire up buttons
  nextBtn.addEventListener("click", () => {
    stopAutoCycle();
    nextSlide();
  });

  prevBtn.addEventListener("click", () => {
    stopAutoCycle();
    prevSlide();
  });

  // Start automatic cycling on load
  startAutoCycle();
});