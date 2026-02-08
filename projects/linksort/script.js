async function loadLinks() {
  try {
    const response = await fetch('links.txt');
    const text = await response.text();
    const lines = text.split('\n').map(l => l.trim()).filter(l => l);

    const groups = {};

    lines.forEach(line => {
      const parts = line.split(' ');
      const link = parts[0];
      const image = parts[1]?.startsWith('http') ? parts[1] : null;
      const title = image ? parts.slice(2).join(' ') : parts.slice(1).join(' ');

      try {
        const url = new URL(link);
        const domainParts = url.hostname.split('.');
        const secondLevel = domainParts.slice(-2).join('.');

        if (!groups[secondLevel]) {
          groups[secondLevel] = [];
        }

        groups[secondLevel].push({ link, image, title });
      } catch (error) {
        console.error(`Invalid URL: ${link}`);
      }
    });

    const tbody = document.querySelector('#links-table tbody');

    Object.keys(groups).sort().forEach(domain => {
      const row = document.createElement('tr');

      const domainCell = document.createElement('td');
      domainCell.textContent = domain;
      row.appendChild(domainCell);

      const linksCell = document.createElement('td');
      groups[domain].forEach(item => {
        const container = document.createElement('div');
        container.style.marginBottom = '8px';

        const a = document.createElement('a');
        a.href = item.link;
        a.textContent = item.link;
        a.target = '_blank';
        container.appendChild(a);

        if (item.image) {
          const img = document.createElement('img');
          img.src = item.image;
          img.className = 'thumbnail';

          img.addEventListener('mouseover', (e) => showTooltip(e, item));
          img.addEventListener('mouseout', hideTooltip);

          container.appendChild(img);
        }

        linksCell.appendChild(container);
      });

      row.appendChild(linksCell);
      tbody.appendChild(row);
    });
  } catch (error) {
    console.error('Error loading links.txt', error);
  }
}

function showTooltip(event, item) {
  const tooltip = document.getElementById('tooltip');
  tooltip.innerHTML = `
    ${item.image ? `<img src="${item.image}" alt="Preview">` : ''}
    ${item.title ? `<p>${item.title}</p>` : ''}
  `;
  tooltip.style.left = `${event.pageX + 10}px`;
  tooltip.style.top = `${event.pageY + 10}px`;
  tooltip.classList.remove('hidden');
}

function hideTooltip() {
  document.getElementById('tooltip').classList.add('hidden');
}

loadLinks();
