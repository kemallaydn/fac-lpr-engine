# Licensed public plate regression fixtures

The image binaries are intentionally not committed. The regression downloads the exact public files listed in `public-manifest.tsv`, verifies their pinned SHA-1 checksums, and then runs the production `fac-lpr-cli` detector/crop/OCR path with exact plate expectations.

Fixtures:

- `38_VU_055.jpg` -> `38VU055`. Source: Wikimedia Commons `File:Turkey_licenceplate.JPG`. Author: Dickelbers. License: CC BY-SA 3.0.
- `34_VZ_7387.jpg` -> `34VZ7387`. Source: Wikimedia Commons `File:Turkeylicenseplate.jpg`. Author: Dantadd. License: Public Domain.

The source page, download URL, author, license, expected recognition and checksum live together in `public-manifest.tsv`. A checksum change is a hard failure and must be reviewed against the source page before the manifest is updated. Recognition expectations must never be changed merely to match a regression.
