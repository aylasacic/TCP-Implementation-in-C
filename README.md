
# Project 2: Transmission Control Protocol

## How to Run

### Step-by-Step Instructions

1. **Navigate to the starter code directory:**
   ```sh
   cd starter_code
   ```

2. **Compile the code using make:**
   ```sh
   make
   ```

3. **Move `sample.txt` to the `obj` folder:**
   - If you are still in the `starter_code` directory:
     ```sh
     mv ../sample.txt ../obj
     ```
   - Alternatively, you can move the file manually to the `obj` folder.

4. **Navigate to the `obj` directory:**
   - If you are still in the `starter_code` directory:
     ```sh
     cd ../obj
     ```
   - Otherwise, just navigate to the `obj` directory.

5. **Open a second terminal and navigate to the `obj` directory in both terminals.**

6. **In the first terminal, run the receiver:**
   ```sh
   ./rdt_receiver 5454 text.txt
   ```

7. **In the second terminal, follow the MAHIMAHI setup instructions found in the repository.**

8. **In the second terminal, run the sender:**
   ```sh
   ./rdt_sender $MAHIMAHI_BASE 5454 sample.txt
   ```

9. **Enjoy! :D**

### Notes
- Ensure that `sample.txt` is in the `obj` directory before running the sender and receiver.
- The MAHIMAHI setup instructions can be found in the repository documentation or setup file. Follow those instructions carefully before running the sender.

### Example
Here is a quick example to illustrate the process:

```sh
# In the first terminal
cd starter_code
make
mv ../sample.txt ../obj
cd ../obj
./rdt_receiver 5454 text.txt

# In the second terminal
cd starter_code/obj
# Follow MAHIMAHI setup instructions
./rdt_sender $MAHIMAHI_BASE 5454 sample.txt
```

By following these steps, you should be able to run the project successfully. If you encounter any issues, please contact me!
