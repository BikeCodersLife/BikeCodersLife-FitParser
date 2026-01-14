import panzoom from 'panzoom';

// Create modal elements
function createModal() {
  const modal = document.createElement('div');
  modal.id = 'mermaid-zoom-modal';
  modal.className = 'mermaid-zoom-modal';
  modal.innerHTML = `
    <div class="mermaid-zoom-backdrop"></div>
    <div class="mermaid-zoom-container">
      <div class="mermaid-zoom-toolbar">
        <button class="mermaid-zoom-btn" data-action="zoom-in" title="Zoom In">+</button>
        <button class="mermaid-zoom-btn" data-action="zoom-out" title="Zoom Out">−</button>
        <button class="mermaid-zoom-btn" data-action="reset" title="Reset">⟲</button>
        <button class="mermaid-zoom-btn mermaid-zoom-close" data-action="close" title="Close">✕</button>
      </div>
      <div class="mermaid-zoom-content">
        <div class="mermaid-zoom-svg-container"></div>
      </div>
      <div class="mermaid-zoom-hint">Scroll to zoom • Drag to pan • Click outside or press Esc to close</div>
    </div>
  `;
  document.body.appendChild(modal);
  return modal;
}

let modal = null;
let panzoomInstance = null;

function getModal() {
  if (!modal) {
    modal = createModal();

    // Close handlers
    modal.querySelector('.mermaid-zoom-backdrop').addEventListener('click', closeModal);
    modal.querySelector('[data-action="close"]').addEventListener('click', closeModal);

    // Toolbar handlers
    modal.querySelector('[data-action="zoom-in"]').addEventListener('click', () => {
      if (panzoomInstance) {
        const transform = panzoomInstance.getTransform();
        panzoomInstance.smoothZoom(window.innerWidth / 2, window.innerHeight / 2, 1.5);
      }
    });

    modal.querySelector('[data-action="zoom-out"]').addEventListener('click', () => {
      if (panzoomInstance) {
        panzoomInstance.smoothZoom(window.innerWidth / 2, window.innerHeight / 2, 0.67);
      }
    });

    modal.querySelector('[data-action="reset"]').addEventListener('click', () => {
      if (panzoomInstance) {
        panzoomInstance.moveTo(0, 0);
        panzoomInstance.zoomAbs(0, 0, 1);
      }
    });

    // Keyboard handler
    document.addEventListener('keydown', (e) => {
      if (e.key === 'Escape' && modal.classList.contains('active')) {
        closeModal();
      }
    });
  }
  return modal;
}

function openModal(svgElement) {
  const modal = getModal();
  const container = modal.querySelector('.mermaid-zoom-svg-container');

  // Clone the SVG
  const svgClone = svgElement.cloneNode(true);
  svgClone.style.maxWidth = 'none';
  svgClone.style.maxHeight = 'none';
  svgClone.style.width = 'auto';
  svgClone.style.height = 'auto';

  // Clear and add new SVG
  container.innerHTML = '';
  container.appendChild(svgClone);

  // Show modal
  modal.classList.add('active');
  document.body.style.overflow = 'hidden';

  // Initialize panzoom
  if (panzoomInstance) {
    panzoomInstance.dispose();
  }
  panzoomInstance = panzoom(container, {
    maxZoom: 10,
    minZoom: 0.1,
    smoothScroll: true,
    zoomDoubleClickSpeed: 1,
  });

  // Center the SVG initially
  requestAnimationFrame(() => {
    const svgRect = svgClone.getBoundingClientRect();
    const containerRect = container.parentElement.getBoundingClientRect();
    const scale = Math.min(
      (containerRect.width * 0.9) / svgRect.width,
      (containerRect.height * 0.9) / svgRect.height,
      1
    );
    const x = (containerRect.width - svgRect.width * scale) / 2;
    const y = (containerRect.height - svgRect.height * scale) / 2;
    panzoomInstance.zoomAbs(0, 0, scale);
    panzoomInstance.moveTo(x, y);
  });
}

function closeModal() {
  if (modal) {
    modal.classList.remove('active');
    document.body.style.overflow = '';
    if (panzoomInstance) {
      panzoomInstance.dispose();
      panzoomInstance = null;
    }
  }
}

function attachClickHandlers() {
  // Find all mermaid diagrams (Docusaurus uses 'docusaurus-mermaid-container' class)
  const mermaidDivs = document.querySelectorAll('.docusaurus-mermaid-container, .mermaid');

  mermaidDivs.forEach((div) => {
    // Skip if already processed
    if (div.dataset.zoomEnabled) return;

    const svg = div.querySelector('svg');
    if (!svg) return;

    div.dataset.zoomEnabled = 'true';

    // Add visual indicator
    div.classList.add('mermaid-zoomable');

    // Add click handler
    div.addEventListener('click', (e) => {
      e.preventDefault();
      e.stopPropagation();
      openModal(svg);
    });
  });
}

// Run on initial load and on route changes
if (typeof window !== 'undefined') {
  // Initial attachment after Mermaid renders
  const observer = new MutationObserver((mutations) => {
    mutations.forEach((mutation) => {
      if (mutation.addedNodes.length) {
        setTimeout(attachClickHandlers, 100);
      }
    });
  });

  // Start observing when DOM is ready
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => {
      observer.observe(document.body, { childList: true, subtree: true });
      setTimeout(attachClickHandlers, 500);
    });
  } else {
    observer.observe(document.body, { childList: true, subtree: true });
    setTimeout(attachClickHandlers, 500);
  }
}

export function onRouteDidUpdate() {
  setTimeout(attachClickHandlers, 500);
}
