#include <iostream>
#include <fstream>
#include "logger.hpp"
#include "molecule.hpp"


int main (int argc, char* argv[])
{
  std::ifstream input("inputfile.mol");
  if (input.is_open()) { std::cout << "input opened\n"; }
  std::ofstream output("out.out");
  if (output.is_open()) { std::cout << "output opened\n"; }
  Logger log(input, output, std::cout);
  std::cout << "log made\n";
  Molecule mol(log, 1);
  std::cout << "molecule made\n";

  log.print(mol, true);
  log.print("\n\n");
  log.print(log.getBasis(), true);
  input.close();
  std::cout << "input closed\n";
  output.close();
  std::cout << "output closed\n";
  return 0;
}
