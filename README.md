# SECRET-GWAS: Confidential Computing for Population-Scale GWAS

## Overview

* [Introduction](#introduction)
* [Installation](#installation)
* [Usage](#usage)
* [Acknowledgements](#acknowledgements)
* [Limitations](#limitations)
* [License](#license)


## Introduction
*SECRET-GWAS* (Scalable Efficient Confidential and privacy-Respecting Environment for Trusted GWAS) is a tool that:
- Parses Hail format files.
- Performs **blazing fast GWAS** using linear or logistic regression.
- Scales to **hundreds** of independent machines and processor cores.
- **Protects against side-channels** like Spectre and control flow/data access pattern leakages.

This allows for collaborative GWAS at a population-scale (millions of patients) that can run in seconds/minutes. With *SECRET-GWAS*, running a query is quick and painless.

This project is currently under active development and welcomes feedback, issues, and suggestions.

## Installation

SECRET-GWAS is developed for Linux and has been tested on Ubuntu 18.04 and 20.04. Use the appropriate installation script:

```
# If using Ubuntu 18.04.
> ./install-18.04.sh

# If using Ubuntu 20.04.
> ./install-20.04.sh
```

Additionally, if you wish to run the demo you will need to install Hail.

```
# For the demo, install Hail and Hail dependencies.
> ./hail_demo/install_reqs.sh
```

## Usage

To run the demo, first see the installation instructions to make sure you have all the prerequisites installed.

Enter the demo directory

```
> cd hail_demo/
```

Active the Python virtual environment

```
> source env/bin/activate
```

You can now use the demo. First run the original Hail demo

```
> python3 hail_gwas.py
```

The output will be written to `Hail_result.vcf`. To run the Hail/*SECRET-GWAS* equivalent that performs the association using *SECRET-GWAS* use the second demo script


```
> python3 SECRET_gwas.py
```

The output will be written to `SECRET_results.vcf`. This can be compared to the original Hail demo output file, as can the two scripts. This is not a true integration into Hail, just a demonstration of how *SECRET-GWAS* parses Hail input files and creates similar output files.

## Acknowledgements
- Inspiration for several design decisions were taken from <a href="https://hail.is/" target = “_blank”>Hail</a>. We also use Hail for the filtering and QC in our GWAS pipeline.
- The data we used for testing is upsampled from a <a href="https://www.internationalgenome.org/" target = “_blank”>1000 Genomes Project</a> snippet provided by <a href="https://hail.is/docs/0.2/tutorials/01-genome-wide-association-study.html" target = “_blank”>Hail</a>.
- We use several third-party libraries for synchronization free data structures, json parsing, and thread pooling. All can be found 
<a href="./shared/third_party" target = “_blank”>here</a>.

## Limitations
The current version of SECRET-GWAS does not yet implement:
- Attestation.
- Imputation methods other than the one used by Hail (average value).

## License
This project is covered under the <a href="LICENSE">MIT</a> license.