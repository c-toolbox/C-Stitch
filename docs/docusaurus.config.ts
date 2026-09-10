import {themes as prismThemes} from 'prism-react-renderer';
import type {Config} from '@docusaurus/types';
import type * as Preset from '@docusaurus/preset-classic';

// This runs in Node.js - Don't use client-side code here (browser APIs, JSX...)

const config: Config = {
  title: 'C-Stitch Documentation',
  tagline:
    'Cubemap to fisheye and fulldome stitcher for immersive image sequences',
  favicon: 'img/favicon.ico',

  // Future flags, see https://docusaurus.io/docs/api/docusaurus-config#future
  future: {
    v4: true, // Improve compatibility with the upcoming Docusaurus v4
  },

  // GitHub Pages serves this site from c-toolbox.github.io/C-Stitch/ (see
  // .github/workflows/pages.yml). CI can override both via DOCS_URL /
  // DOCS_BASE_URL if the site is served from a different origin or path.
  url: process.env.DOCS_URL || 'https://c-toolbox.github.io',
  baseUrl: process.env.DOCS_BASE_URL || '/C-Stitch/',
  trailingSlash: true,

  organizationName: 'c-toolbox',
  projectName: 'C-Stitch',

  onBrokenLinks: 'throw',

  markdown: {
    // future.v4 would otherwise parse .md as plain CommonMark, disabling admonitions.
    format: 'mdx',
    hooks: {
      onBrokenMarkdownLinks: 'warn',
    },
  },

  i18n: {
    defaultLocale: 'en',
    locales: ['en'],
  },

  presets: [
    [
      'classic',
      {
        docs: {
          // Serve the docs at the site root, so pages live at /install/ etc.
          // instead of /docs/install/. The intro doc (slug: '/') is the
          // landing page at /.
          routeBasePath: '/',
          sidebarPath: './sidebars.ts',
        },
        blog: false,
        theme: {
          customCss: './src/css/custom.css',
        },
      } satisfies Preset.Options,
    ],
  ],

  plugins: [
    [
      '@cmfcmf/docusaurus-search-local',
      {
        indexDocs: true, // docs are the main content of this site
        indexBlog: false, // blog is disabled in the classic preset above
        indexPages: false, // only a homepage exists as static page
        language: 'en',
      },
    ],
  ],

  themeConfig: {
    image: 'img/logo.png',
    colorMode: {
      respectPrefersColorScheme: true,
    },
    navbar: {
      title: 'C-Stitch',
      logo: {
        alt: 'C-Stitch',
        src: 'img/logo.png',
      },
      items: [
        {
          type: 'docSidebar',
          sidebarId: 'docsSidebar',
          position: 'left',
          label: 'Documentation',
        },
        {to: '/install', label: 'Install', position: 'left'},
        {to: '/workflow/overview', label: 'Workflow', position: 'left'},
        {to: '/media/overview', label: 'Media', position: 'left'},
        {to: '/build/overview', label: 'Build', position: 'left'},
        {
          href: 'https://github.com/c-toolbox/C-Stitch',
          label: 'GitHub',
          position: 'right',
        },
      ],
    },
    footer: {
      style: 'dark',
      links: [
        {
          title: 'Getting started',
          items: [
            {label: 'Install', to: '/install'},
            {label: 'Setup', to: '/setup/overview'},
            {label: 'Media and data structure', to: '/media/overview'},
          ],
        },
        {
          title: 'Workflow',
          items: [
            {label: 'Stitch workflow', to: '/workflow/overview'},
            {label: 'C-Job architecture', to: '/workflow/cjob-architecture'},
          ],
        },
        {
          title: 'Development',
          items: [
            {label: 'Build from code', to: '/build/overview'},
            {label: 'Versions', to: '/versions'},
          ],
        },
      ],
      copyright: `Copyright © ${new Date().getFullYear()} Erik Sundén. Distributed under the <a href="https://github.com/c-toolbox/C-Stitch/blob/master/LICENSE.txt">GNU General Public License v3.0</a>.`,
    },
    prism: {
      theme: prismThemes.github,
      darkTheme: prismThemes.dracula,
    },
  } satisfies Preset.ThemeConfig,
};

export default config;
